// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>
#include <threads.h>

typedef struct SensorSmokeState {
    int validated;
} SensorSmokeState;

typedef struct ReadingState {
    int calls;
    CNA_AccelerometerReading last;
} ReadingState;

typedef struct HookState {
    int calls;
} HookState;

typedef struct WrongThreadState {
    CNA_AccelerometerHandle sensor;
    CNA_Result result;
} WrongThreadState;

static void on_reading(const CNA_AccelerometerReading* const reading, void* const context)
{
    ReadingState* const state = (ReadingState*)context;
    state->last = *reading;
    ++state->calls;
}

static void on_disposal(void* const context)
{
    ++((HookState*)context)->calls;
}

/* No verification machine has motion-sensor hardware, so the suite drives the supported path
   through the canonical test-support hooks instead of skipping it. */
static int validate_accelerometer(const CNA_Handle game)
{
    CNA_AccelerometerHandle sensor = CNA_INVALID_HANDLE;
    CNA_AccelerometerHandle rejected = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle registration = CNA_INVALID_HANDLE;
    CNA_AccelerometerReading reading;
    CNA_SensorState state = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    ReadingState readings = {0, {0U, 0U, {0, 0}, {0.0F, 0.0F, 0.0F}}};
    HookState hook = {0};
    int32_t error_id = 0;
    int64_t ticks = INT64_C(-1);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    if (cna_accelerometer_get_is_supported(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_accelerometer_get_is_supported(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_accelerometer_create(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_accelerometer_create(game, &sensor) != CNA_RESULT_SUCCESS ||
        sensor == CNA_INVALID_HANDLE) {
        return 0;
    }
    {
        CNA_Bool valid = UINT8_C(9);
        if (cna_accelerometer_get_state(sensor, &state) != CNA_RESULT_SUCCESS ||
            state > CNA_SENSOR_STATE_MAXIMUM ||
            cna_accelerometer_get_is_data_valid(sensor, &valid) != CNA_RESULT_SUCCESS ||
            valid != CNA_FALSE) {
            return 0;
        }
    }
    /* An unsupported sensor refuses the reading query rather than answering a default, because the
       canonical property throws there. No verification machine has the hardware, so this is the
       branch that actually runs here. */
    memset(&reading, 9, sizeof(reading));
    if (flag == CNA_FALSE &&
        cna_accelerometer_get_current_value(sensor, &reading) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    /* The update interval round-trips. */
    if (cna_accelerometer_set_time_between_updates_ticks(sensor, INT64_C(200000)) !=
            CNA_RESULT_SUCCESS ||
        cna_accelerometer_get_time_between_updates_ticks(sensor, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(200000)) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_accelerometer_get_type_name_size(sensor, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_accelerometer_copy_type_name(sensor, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Devices.Sensors.Accelerometer") != 0) {
        return 0;
    }

    /* Forcing the supported flag lets the start path run on a machine with no sensor at all. */
    if (cna_accelerometer_set_supported_for_tests_ext(sensor, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_subscribe_current_value_changed(sensor, 0, &readings, &registration) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_accelerometer_subscribe_current_value_changed(sensor, on_reading, &readings, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_accelerometer_subscribe_current_value_changed(
            sensor, on_reading, &readings, &registration) != CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* A synthetic reading travels the real dispatch path, so the callback sees it -- converted
       from the platform's metres per second squared into the canonical g, which is why the value
       that comes back is the injected one divided by standard gravity rather than itself. */
    if (cna_accelerometer_set_started_for_tests_ext(sensor, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_inject_synthetic_update_ext(sensor, 0.0F, 0.0F, 9.80665F) !=
            CNA_RESULT_SUCCESS ||
        readings.calls != 1 || readings.last.acceleration.x != 0.0F ||
        readings.last.acceleration.z < 0.999F || readings.last.acceleration.z > 1.001F) {
        return 0;
    }
    /* The same reading is now the sensor's current value, and the data is valid. */
    if (cna_accelerometer_get_current_value(sensor, &reading) != CNA_RESULT_SUCCESS ||
        reading.acceleration.z < 0.999F || reading.acceleration.z > 1.001F ||
        cna_accelerometer_get_is_data_valid(sensor, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    /* Releasing the registration detaches it. */
    if (cna_sensor_unsubscribe_ext(registration) != CNA_RESULT_SUCCESS ||
        cna_sensor_unsubscribe_ext(registration) != CNA_RESULT_INVALID_HANDLE ||
        cna_accelerometer_inject_synthetic_update_ext(sensor, 1.0F, 1.0F, 1.0F) !=
            CNA_RESULT_SUCCESS ||
        readings.calls != 1) {
        return 0;
    }

    /* The dispatcher walks a registered instance list, and an explicit set can be dispatched. */
    {
        const CNA_AccelerometerHandle sensors[1] = {sensor};
        if (cna_accelerometer_register_started_instance_for_tests_ext(sensor) !=
                CNA_RESULT_SUCCESS ||
            cna_accelerometer_dispatch_to_instances_for_tests_ext(
                game, sensors, UINT64_C(1), 0.0F, 9.80665F, 0.0F) != CNA_RESULT_SUCCESS ||
            cna_accelerometer_get_current_value(sensor, &reading) != CNA_RESULT_SUCCESS ||
            reading.acceleration.y < 0.999F || reading.acceleration.y > 1.001F ||
            cna_accelerometer_dispatch_to_instances_for_tests_ext(
                game, 0, UINT64_C(1), 0.0F, 0.0F, 0.0F) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_accelerometer_unregister_started_instance_for_tests_ext(sensor) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* The dispatcher's swallowed-exception counters are readable. */
    {
        int32_t count = -1;
        if (cna_accelerometer_get_dispatch_exception_count_for_tests_ext(game, &count) !=
                CNA_RESULT_SUCCESS ||
            count < 0 ||
            cna_accelerometer_get_last_dispatch_exception_message_size_for_tests_ext(
                game, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(text) ||
            cna_accelerometer_copy_last_dispatch_exception_message_for_tests_ext(
                game, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    /* Both process-wide test switches are restored after use. */
    if (cna_accelerometer_set_event_watch_registration_failure_for_tests_ext(game, CNA_TRUE) !=
            CNA_RESULT_SUCCESS ||
        cna_accelerometer_set_event_watch_registration_failure_for_tests_ext(game, CNA_FALSE) !=
            CNA_RESULT_SUCCESS ||
        cna_accelerometer_is_sensor_connected_for_tests_ext(game, INT64_C(0), &flag) !=
            CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_accelerometer_get_subsystem_held_for_tests_ext(sensor, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE)) {
        return 0;
    }

    /* Starting twice is the canonical failure that carries an error identifier. */
    if (cna_accelerometer_set_started_for_tests_ext(sensor, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_accelerometer_start(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_sensors_get_last_error_id_ext(&error_id, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_sensors_get_last_error_id_ext(0, &flag) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The disposal hook runs during disposal. Disposing a second time is refused rather than
       ignored: the canonical sensor treats it as use-after-disposal, unlike most disposables in
       this ABI. */
    if (cna_accelerometer_set_disposal_cleanup_hook_for_tests_ext(sensor, on_disposal, &hook) !=
            CNA_RESULT_SUCCESS ||
        cna_accelerometer_dispose(sensor) != CNA_RESULT_SUCCESS || hook.calls != 1 ||
        cna_accelerometer_dispose(sensor) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    /* Every acquisition route refuses a disposed sensor, which is how C observes the state the
       canonical class keeps to itself. */
    if (cna_accelerometer_start(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_accelerometer_stop(sensor) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }

    return cna_accelerometer_destroy(sensor) == CNA_RESULT_SUCCESS &&
        cna_accelerometer_destroy(sensor) == CNA_RESULT_INVALID_HANDLE &&
        cna_accelerometer_get_state(rejected, &state) == CNA_RESULT_INVALID_HANDLE &&
        cna_accelerometer_start(rejected) == CNA_RESULT_INVALID_HANDLE &&
        cna_accelerometer_get_current_value(rejected, &reading) == CNA_RESULT_INVALID_HANDLE;
}

static void on_gyro_reading(const CNA_GyroscopeReading* const reading, void* const context)
{
    ReadingState* const state = (ReadingState*)context;
    state->last.acceleration = reading->rotation_rate;
    ++state->calls;
}

static int validate_gyroscope(const CNA_Handle game)
{
    CNA_GyroscopeHandle sensor = CNA_INVALID_HANDLE;
    CNA_SensorEventRegistrationHandle registration = CNA_INVALID_HANDLE;
    CNA_GyroscopeReading reading;
    CNA_SensorState state = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    ReadingState readings = {0, {0U, 0U, {0, 0}, {0.0F, 0.0F, 0.0F}}};
    HookState hook = {0};
    int64_t ticks = INT64_C(0);
    uint64_t bytes = UINT64_C(9);
    char text[256];

    if (cna_gyroscope_get_is_supported(game, &flag) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_create(game, &sensor) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_get_state(sensor, &state) != CNA_RESULT_SUCCESS ||
        state > CNA_SENSOR_STATE_MAXIMUM) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_gyroscope_get_type_name_size(sensor, &bytes) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_copy_type_name(sensor, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Devices.Sensors.Gyroscope") != 0) {
        return 0;
    }
    if (cna_gyroscope_set_supported_for_tests_ext(sensor, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_set_started_for_tests_ext(sensor, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_subscribe_current_value_changed(
            sensor, on_gyro_reading, &readings, &registration) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_inject_synthetic_update_ext(sensor, 0.125F, 0.25F, 0.5F) !=
            CNA_RESULT_SUCCESS ||
        readings.calls != 1 || readings.last.acceleration.z != 0.5F ||
        cna_gyroscope_get_current_value(sensor, &reading) != CNA_RESULT_SUCCESS ||
        reading.rotation_rate.x != 0.125F || reading.rotation_rate.y != 0.25F ||
        cna_gyroscope_get_is_data_valid(sensor, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    /* CBIND-065: everything below already existed for the accelerometer and was simply never
       written for the gyroscope, which is why `check_route_test_coverage.py` measured thirteen
       gyroscope routes that no test named while the matrix recorded both sensors implemented
       against this same file. The two classes are separate canonical types with separate
       process-wide test state, so the accelerometer's assertions say nothing about these. */

    /* The update interval round-trips. */
    if (cna_gyroscope_set_time_between_updates_ticks(sensor, INT64_C(200000)) !=
            CNA_RESULT_SUCCESS ||
        cna_gyroscope_get_time_between_updates_ticks(sensor, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(200000)) {
        return 0;
    }

    /* The dispatcher walks a registered instance list, and an explicit set can be dispatched. */
    {
        const CNA_GyroscopeHandle sensors[1] = {sensor};
        if (cna_gyroscope_register_started_instance_for_tests_ext(sensor) != CNA_RESULT_SUCCESS ||
            cna_gyroscope_dispatch_to_instances_for_tests_ext(
                game, sensors, UINT64_C(1), 1.5F, 2.5F, 3.5F) != CNA_RESULT_SUCCESS ||
            cna_gyroscope_get_current_value(sensor, &reading) != CNA_RESULT_SUCCESS ||
            reading.rotation_rate.x != 1.5F || reading.rotation_rate.z != 3.5F ||
            cna_gyroscope_dispatch_to_instances_for_tests_ext(
                game, 0, UINT64_C(1), 0.0F, 0.0F, 0.0F) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_gyroscope_unregister_started_instance_for_tests_ext(sensor) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* The dispatcher's swallowed-exception counters are readable. */
    {
        int32_t count = -1;
        if (cna_gyroscope_get_dispatch_exception_count_for_tests_ext(game, &count) !=
                CNA_RESULT_SUCCESS ||
            count < 0 ||
            cna_gyroscope_get_last_dispatch_exception_message_size_for_tests_ext(game, &bytes) !=
                CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(text) ||
            cna_gyroscope_copy_last_dispatch_exception_message_for_tests_ext(
                game, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* Both process-wide test switches are restored after use. */
    if (cna_gyroscope_set_event_watch_registration_failure_for_tests_ext(game, CNA_TRUE) !=
            CNA_RESULT_SUCCESS ||
        cna_gyroscope_set_event_watch_registration_failure_for_tests_ext(game, CNA_FALSE) !=
            CNA_RESULT_SUCCESS ||
        cna_gyroscope_is_sensor_connected_for_tests_ext(game, INT64_C(0), &flag) !=
            CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_gyroscope_get_subsystem_held_for_tests_ext(sensor, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE)) {
        return 0;
    }

    /* Starting an already-started sensor is the canonical refusal. */
    if (cna_gyroscope_start(sensor) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }

    /* The disposal hook runs during disposal, and a disposed sensor refuses every acquisition
       route -- the same contract the accelerometer proves, on its own class. */
    if (cna_gyroscope_set_disposal_cleanup_hook_for_tests_ext(sensor, on_disposal, &hook) !=
            CNA_RESULT_SUCCESS ||
        cna_sensor_unsubscribe_ext(registration) != CNA_RESULT_SUCCESS ||
        cna_gyroscope_dispose(sensor) != CNA_RESULT_SUCCESS || hook.calls != 1 ||
        cna_gyroscope_dispose(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_gyroscope_start(sensor) != CNA_RESULT_INVALID_STATE ||
        cna_gyroscope_stop(sensor) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    return cna_gyroscope_destroy(sensor) == CNA_RESULT_SUCCESS &&
        cna_gyroscope_destroy(sensor) == CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    SensorSmokeState* const state = (SensorSmokeState*)context;
    if (game_time == 0 || !validate_accelerometer(game) || !validate_gyroscope(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int query_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_SensorState sensor_state = CNA_SENSOR_STATE_NOT_SUPPORTED;
    state->result = cna_accelerometer_get_state(state->sensor, &sensor_state);
    return 0;
}

int main(void)
{
    SensorSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API sensor device smoke", UINT64_C(25)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return CNA_TEST_FAIL(1);
    }

    CNA_AccelerometerHandle sensor = CNA_INVALID_HANDLE;
    if (cna_accelerometer_create(game, &sensor) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(2);
    }
    WrongThreadState wrong_thread = {sensor, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, query_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.result != CNA_RESULT_THREAD) {
        return CNA_TEST_FAIL(3);
    }
    if (cna_accelerometer_destroy(sensor) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(4);
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(5);
    }
    return 0;
}
