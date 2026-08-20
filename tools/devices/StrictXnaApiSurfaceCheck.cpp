// SPDX-License-Identifier: MS-PL
//
// Task VERIFY-003/DEV-API-002: a compile-only check that the genuine XNA 4.0/
// WP7 `Microsoft::Devices`/`Microsoft::Devices::Sensors` API surface remains
// fully usable when `CNA_STRICT_XNA_API` is defined (see
// `include/CNA/CNAHelper.hpp`'s `CNAEXT` macro, which expands to
// `[[deprecated]]` instead of nothing under that macro). This translation
// unit is compiled with `-Werror=deprecated-declarations` (see
// `CMakeLists.txt`'s `cna_strict_xna_api_check` target) specifically so it
// FAILS TO BUILD if it — or, more importantly, a future edit to this file —
// ever calls a `CNAEXT`-tagged member. It deliberately calls ONLY members this
// plan's own audits (`DEV-API-001`/`DEV-API-004`/`READINGS-001`/`READINGS-002`)
// confirmed are genuinely real XNA/WP7 API, never a `CNAEXT` one, so that a
// clean build of this file is itself evidence the real API surface is
// unaffected by strict mode.
//
// Lives under tools/, not tests/: tests/*.cpp is glob-collected into the
// single CnaTests gtest binary (one shared main() from gtest_main), and this
// file's own main() would conflict with that — mirrors tools/net/'s existing
// standalone-executable-not-part-of-CnaTests precedent.
//
// Deliberately NOT calling: VibrateController::getIsSupportedProperty()/
// getDeviceNameProperty()/StartLeftRight()/the intensity Start() overload
// (all CNAEXT); Accelerometer::InjectSyntheticSensorUpdate()/*ForTesting()
// (all CNAEXT); Gyroscope/Compass/Motion::getStateProperty() (CNAEXT on those
// three specifically — only Accelerometer::getStateProperty() is real, see
// `DEV-API-003`'s resolution in plans/plan_devices.md); any reading struct's
// operator==/operator!=/ToString()/GetHashCode() (all CNAEXT per
// `DEV-API-004`); SensorBase<T>::TimeBetweenUpdatesChanged (CNAEXT per
// `SENSORBASE-007`).

#include "Microsoft/Devices/VibrateController.hpp"
#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/Compass.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/Motion.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/Exception.hpp"
#include "System/TimeSpan.hpp"

using namespace Microsoft::Devices;
using namespace Microsoft::Devices::Sensors;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    void ExerciseVibrateController()
    {
        VibrateController* controller = VibrateController::getDefaultProperty();
        try
        {
            controller->Start(System::TimeSpan::FromSeconds(1));
        }
        catch (const System::Exception&)
        {
        }
        controller->Stop();
    }

    void ExerciseAccelerometer()
    {
        Accelerometer accelerometer;
        (void)Accelerometer::getIsSupportedProperty();
        (void)accelerometer.getStateProperty();

        accelerometer.CurrentValueChanged +=
            [](System::Object*, const SensorReadingEventArgs<AccelerometerReading>&) {};
        accelerometer.ReadingChanged +=
            [](System::Object*, const AccelerometerReadingEventArgs&) {};

        try
        {
            accelerometer.Start();
        }
        catch (const System::Exception&)
        {
        }
        accelerometer.Stop();

        try
        {
            (void)accelerometer.getCurrentValueProperty();
        }
        catch (const System::Exception&)
        {
        }
        (void)accelerometer.getIsDataValidProperty();
        accelerometer.setTimeBetweenUpdatesProperty(accelerometer.getTimeBetweenUpdatesProperty());

        accelerometer.Dispose();
    }

    void ExerciseGyroscope()
    {
        Gyroscope gyroscope;
        (void)Gyroscope::getIsSupportedProperty();

        gyroscope.CurrentValueChanged +=
            [](System::Object*, const SensorReadingEventArgs<GyroscopeReading>&) {};

        try
        {
            gyroscope.Start();
        }
        catch (const System::Exception&)
        {
        }
        gyroscope.Stop();

        try
        {
            (void)gyroscope.getCurrentValueProperty();
        }
        catch (const System::Exception&)
        {
        }
        (void)gyroscope.getIsDataValidProperty();
        gyroscope.setTimeBetweenUpdatesProperty(gyroscope.getTimeBetweenUpdatesProperty());

        gyroscope.Dispose();
    }

    void ExerciseCompass()
    {
        Compass compass;
        (void)Compass::getIsSupportedProperty();

        compass.CurrentValueChanged +=
            [](System::Object*, const SensorReadingEventArgs<CompassReading>&) {};
        compass.Calibrate += [](System::Object*, const CalibrationEventArgs&) {};

        try
        {
            compass.Start();
        }
        catch (const System::Exception&)
        {
        }
        compass.Stop();

        try
        {
            (void)compass.getCurrentValueProperty();
        }
        catch (const System::Exception&)
        {
        }
        (void)compass.getIsDataValidProperty();
        compass.setTimeBetweenUpdatesProperty(compass.getTimeBetweenUpdatesProperty());

        compass.Dispose();
    }

    void ExerciseMotion()
    {
        Motion motion;
        (void)Motion::getIsSupportedProperty();

        motion.CurrentValueChanged +=
            [](System::Object*, const SensorReadingEventArgs<MotionReading>&) {};
        motion.Calibrate += [](System::Object*, const CalibrationEventArgs&) {};

        try
        {
            motion.Start();
        }
        catch (const System::Exception&)
        {
        }
        motion.Stop();

        try
        {
            (void)motion.getCurrentValueProperty();
        }
        catch (const System::Exception&)
        {
        }
        (void)motion.getIsDataValidProperty();
        motion.setTimeBetweenUpdatesProperty(motion.getTimeBetweenUpdatesProperty());

        motion.Dispose();
    }

    void ExerciseReadingStructs()
    {
        const System::DateTimeOffset timestamp = System::DateTimeOffset::getUtcNowProperty();

        const AccelerometerReading accelerometerReading(timestamp, Vector3::Zero);
        (void)accelerometerReading.getTimestampProperty();
        (void)accelerometerReading.getAccelerationProperty();

        const GyroscopeReading gyroscopeReading(Vector3::Zero, timestamp);
        (void)gyroscopeReading.getRotationRateProperty();
        (void)gyroscopeReading.getTimestampProperty();

        const CompassReading compassReading(0.0, 0.0, Vector3::Zero, timestamp, 0.0);
        (void)compassReading.getHeadingAccuracyProperty();
        (void)compassReading.getMagneticHeadingProperty();
        (void)compassReading.getMagnetometerReadingProperty();
        (void)compassReading.getTimestampProperty();
        (void)compassReading.getTrueHeadingProperty();

        const AttitudeReading attitudeReading(
            0.0f, 0.0f, 0.0f, Quaternion::Identity, Matrix::getIdentityProperty(), timestamp);
        (void)attitudeReading.getPitchProperty();
        (void)attitudeReading.getRollProperty();
        (void)attitudeReading.getYawProperty();
        (void)attitudeReading.getQuaternionProperty();
        (void)attitudeReading.getRotationMatrixProperty();
        (void)attitudeReading.getTimestampProperty();

        const MotionReading motionReading(
            attitudeReading, Vector3::Zero, Vector3::Zero, Vector3::Zero, timestamp);
        (void)motionReading.getAttitudeProperty();
        (void)motionReading.getDeviceAccelerationProperty();
        (void)motionReading.getDeviceRotationRateProperty();
        (void)motionReading.getGravityProperty();
        (void)motionReading.getTimestampProperty();
    }
} // namespace

int main()
{
    ExerciseVibrateController();
    ExerciseAccelerometer();
    ExerciseGyroscope();
    ExerciseCompass();
    ExerciseMotion();
    ExerciseReadingStructs();
    return 0;
}
