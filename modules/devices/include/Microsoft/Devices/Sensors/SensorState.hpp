// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/7/25.
//

#pragma once

namespace Microsoft::Devices::Sensors
{
    /**
     * @brief Specifies the current state of a sensor.
     *
     * This is a C++ counterpart of the .NET
     * Microsoft.Devices.Sensors.SensorState enumeration. `Accelerometer::State`
     * is the one real, strict-XNA-API member of this enum's real WP7 users
     * (`Gyroscope`/`Compass`/`Motion`'s own `State` properties are `CNAEXT`
     * extensions added for symmetry — see each class's own `getStateProperty()`
     * doc comment).
     *
     * Task BASE2-003 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
     * this codebase's current, verified reachability per value, confirmed by
     * reading every `state_ = SensorState::...` assignment in
     * `Accelerometer.cpp`/`Gyroscope.cpp`/`Compass.cpp`/`Motion.cpp` directly
     * (not assumed): `NotSupported`/`Initializing`/`Ready`/`Disabled` are
     * produced by all four classes today; `NoData`/`NoPermissions` are
     * produced by **none** of them. This is documented honestly as "currently
     * never produced, unverified against real WP7 behavior" rather than
     * "intentionally never produced" — this codebase has no local WP7 SDK/
     * MonoGame reference to confirm whether real `Accelerometer.State` (the
     * one strict-API member) is actually documented to ever report `NoData`/
     * `NoPermissions`, or under what real-device condition (e.g. Android's
     * basic motion sensors — accelerometer/gyroscope/magnetometer/rotation
     * vector — do not require a runtime permission grant in Android's own
     * permission model, which would make `NoPermissions` plausibly
     * unreachable on this project's supported platforms specifically, but
     * this has not been independently confirmed against Android's official
     * documentation the way other platform-contract claims in this codebase
     * have been). See `plans/plan_devices.md`'s `BASE2-003`/`DEVPERF-002`/`003`
     * entries for the full reasoning and what remains open.
     */
    enum class SensorState
    {
        /** @brief The sensor is not supported on this device. Reachable — produced by all four sensor classes. */
        NotSupported,
        /** @brief The sensor is ready and providing data. Reachable — produced by all four sensor classes. */
        Ready,
        /** @brief The sensor is currently initializing. Reachable — produced by all four sensor classes (the initial state before `Start()` is ever called, when the sensor type is supported). */
        Initializing,
        /** @brief The sensor has no data available. Currently never produced by any of the four sensor classes — see this enum's own class-level doc comment for why this is not yet claimed to be intentional. */
        NoData,
        /** @brief The sensor cannot be accessed due to missing permissions. Currently never produced by any of the four sensor classes — see this enum's own class-level doc comment for why this is not yet claimed to be intentional. */
        NoPermissions,
        /** @brief The sensor is disabled. Reachable — produced by all four sensor classes (set by `Stop()`). */
        Disabled,
    };
} // namespace Microsoft::Devices::Sensors
