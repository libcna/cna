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
