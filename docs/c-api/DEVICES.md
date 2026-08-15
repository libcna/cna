# CNA C API — devices and sensors

`sensors.h` and its siblings map the `Microsoft::Devices` and `CNA::Devices` modules: the device's
own motion sensors and their readings, vibration, and the CNA system-service extensions. This page
records only what a consumer cannot read off the headers; the general contracts live in
[`HANDLES.md`](HANDLES.md), [`OWNERSHIP.md`](OWNERSHIP.md),
[`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md), [`ERRORS.md`](ERRORS.md) and
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

The family is being mapped in slices; [`COVERAGE.md`](COVERAGE.md) is authoritative about which rows
exist today.

## Timestamps

Sensor readings carry `CNA_DateTimeOffset`, the ABI form of the runtime's date-and-offset type: two
100-nanosecond tick counts — the unit every duration in this ABI already uses — where the first is
local time counted from **0001-01-01** rather than from the Unix epoch, and the second is the offset
from UTC. That base is the canonical type's own; the picture date in `media_library.h` counts from
the Unix epoch for the same reason, because *its* canonical type does. Subtracting the offset gives
UTC, which is what the canonical equality and hashing compare.

## Sensor readings

Each of the five canonical readings is a fixed value rather than a handle: they are copyable
snapshots with no identity, and the canonical types are the same. Every reading carries a timestamp,
which is how the canonical reading *interface* is expressed here — C gets the guarantee the
interface exists to make, without an abstract base it could not use.

`cna_*_reading_init_from_values` follows each canonical constructor's **own argument order**, even
where that is inconsistent between readings: the accelerometer takes its timestamp first, the
gyroscope takes its rate first, and the compass takes the true heading last. Normalizing them would
make the C API easier to remember and harder to check against the canonical source, so it is not
done.

Three canonical behaviors are preserved rather than tidied:

- Equality pairs the reading's values **with its timestamp**, so two readings of the same value at
  different instants are not equal.
- The text conversions carry only part of the reading. The accelerometer's names only the
  acceleration, the gyroscope's only the rate, and the **motion reading's names only the device
  acceleration and gravity** — not the attitude or the rotation rate a reader might expect.
- All six sensor-state identities are exposed, including the two the canonical header records as
  currently unreachable. An identity is not a claim that something produces it, and hiding them
  would renumber the rest.

## Sensor devices

Each concrete sensor is an owned handle. The canonical common base is a **class template**, so C
does not model it as a type: its contract — current value, data validity, update interval, start,
stop, dispose and the reading-changed event — appears once per concrete sensor, and the event
delivers the **reading itself** rather than the event-argument wrapper, which holds nothing else.

Three canonical behaviors are reported rather than smoothed over:

- **Reading the current value of an unsupported sensor fails** with `CNA_RESULT_INVALID_STATE`; the
  canonical property throws there rather than answering a default. Check the support probe first.
- **Disposing twice is refused**, unlike most disposables in this ABI: the canonical sensor treats a
  second disposal as use-after-disposal. Every other route reports the same failure afterwards,
  which is how a caller observes the disposed state — the canonical flag is protected, so there is
  no query route for it, and none was invented.
- The canonical **protected** members — the interval-changed event, the disposal flag, the
  value-publishing setters and the throttle — exist for derived classes. C cannot derive, so they
  have no routes at all.

**The test-support surface is mapped, deliberately.** No machine this ABI is verified on has motion
sensors, so `cna_<sensor>_set_supported_for_tests_ext` and `cna_<sensor>_inject_synthetic_update_ext`
are what let a C consumer — and this suite — drive the supported path and the real dispatch chain
instead of only ever seeing "not supported". The injector takes **platform units** and the reading
comes back in the canonical unit: inject 9.80665 m/s² into an accelerometer and read 1 g.

A sensor failure carries an **error identifier** the message does not spell out. The exception
firewall records it per thread, readable with `cna_sensors_get_last_error_id_ext` — the same
treatment a network join failure already gets. Read it immediately after the failing call.

## The compass, fused motion and the reading events

Both remaining sensors are the same owned handle as the motion sensors, with two additions.

**The canonical compass supports one platform, and it is not any platform this ABI is verified on.**
Its unsupported refusal is therefore the branch a desktop consumer will actually hit — a real answer
about the device, not a hole in the binding. The same is true of fused motion. To reach anything past
that refusal, this ABI supplies its own backend: `cna_<sensor>_set_test_backend_ext` installs it,
`_inject_synthetic_update_ext` delivers a reading through the canonical path, and
`_inject_calibration_request_ext` raises the calibration event. The canonical hook takes a
caller-implemented backend object, which C cannot write; the switch is what C gets instead. Swapping
a backend while acquisition is running is refused, so a started session can never lose the object
delivering its readings.

`cna_motion_get_is_attitude_north_referenced_ext` answers whether the yaw has an absolute reference.
**Its default is vacuous on purpose**: with no backend, or before starting, it answers `CNA_TRUE`,
which means "nothing is drifting yet", not "north is known". Only a started backend's own answer is
informative, which is why the test backend takes that claim as an argument.

Two canonical limits are reported rather than smoothed. Ten simultaneous instances of a sensor is the
canonical ceiling, and the eleventh creation fails rather than degrading. Stopping a sensor that
never started still succeeds and still moves the state to disabled.

### Event arguments

Three canonical event-argument types resolve three different ways, and the difference is the payload:

- `SensorReadingEventArgs<T>` is a class template wrapping one reading and nothing else, so it is
  **flattened**: every current-value callback delivers the reading itself.
- The calibration argument carries **no data at all**, so it becomes a callback with no payload —
  `CNA_SensorEventCallback`, context only. Only its type name survives, as a value-free route pair.
- The legacy accelerometer reading argument carries the acceleration as **three separate components**
  rather than a vector, so it is a value of its own, `CNA_AccelerometerReadingEventInfo`, with the
  full set of value routes.

`cna_accelerometer_subscribe_reading_changed` is the obsolete counterpart of the current-value
subscription, mapped for completeness. Both fire for one reading and the canonical order is fixed —
current value first, legacy second — which this ABI reports rather than reserves the right to change.
Unlike the current-value event, the legacy one is raised only when the reading is valid.
