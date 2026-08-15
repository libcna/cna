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

## Vibration and the host services

`devices.h` carries two different things, and the difference matters to a consumer. **Vibration is
always there**: it belongs to the canonical XNA-era device layer, and its routes answer in every
build. **Everything else is the CNA device extension**, compiled in or out with one build option, so
`cna_devices_ext_is_available` is the first thing to call — every `_ext` route below it is exported
either way and reports `CNA_RESULT_NOT_SUPPORTED` when the layer is absent, so the ABI's symbol set
never depends on a build option. Read a refusal as "this build has no extension layer", not as "this
machine has no such device".

The vibration controller is a **process-wide singleton**, so it has no handle: every route addresses
the one controller and takes the game handle only for thread affinity. Its canonical asymmetry is
preserved rather than tidied — the **duration is bounded** and a request outside zero to five seconds
is refused, while the **intensity is clamped**, and a not-a-number strength becomes no vibration at
all rather than reaching the platform as an undefined value.

### The two clipboards are one clipboard

The extension layer and the input module both expose a clipboard, and they wrap **the same platform
clipboard**. This ABI does not give a consumer two names for one answer: the reads stay
`cna_clipboard_get_text_size`/`_copy_text`/`_get_has_text`. The only thing the extension adds is that
setting reports whether the platform accepted the text, which the input setter discards — that is
`cna_devices_clipboard_set_text_ext`. Acceptance means the request was taken, not that a later read
returns it.

### Routes a test cannot complete, and what this ABI does about them

Four of these services end in something no automated caller can finish: a modal dialog, an
asynchronous file picker, a tray icon on a real desktop, a motor no verification machine has. Three
of them have a canonical backend seam, so this ABI supplies the backend C cannot write and exposes
only the switch — `cna_vibrate_controller_set_test_backend_ext`,
`cna_message_box_set_test_backend_ext`, `cna_file_dialog_set_test_backend_ext` — plus a log or a
result to read back. The tray takes its backend as a **second constructor** canonically, so it gets
`cna_system_tray_create_with_test_backend_ext` rather than a switch, and
`cna_system_tray_click_entry_for_tests_ext` to activate an entry.

The message box and file dialog backends are **process-wide**, exactly as canonically, so those
switches are not scoped to the game handle they validate.

`cna_url_launcher_open_ext` is the one route with no seam. It hands control to another application,
so this ABI's own suite never calls it with a real URL — only its refusals are covered. That is a
deliberate gap in the evidence, recorded rather than papered over.

Two more canonical behaviors are reported rather than corrected: a tray entry index past the last
entry is **ignored** by the mutators and reads false instead of being refused, and a session with no
native window answers a content scale of zero and an empty safe area rather than failing.

## The camera

A camera is an owned handle, and its two availability questions are deliberately separate: the probe
answers whether the platform has a **camera driver** at all, and the enumeration answers how many
cameras it currently reports. A driver is not a camera.

A frame lands in a `Texture2D` **the caller owns and keeps**. That is the canonical contract, and it
is deliberately not the video player's borrowed per-frame texture: nothing here is invalidated by the
next call, because nothing here is lent. Two canonical behaviors are preserved rather than corrected —
having no frame ready is an ordinary `CNA_FALSE` rather than a failure, and **a texture whose size
does not match the frame is refused the same way**, with no resize and no way to tell the two cases
apart. Read the frame size first and size the texture to it.

The canonical class takes its backend as a constructor argument, so the test seam is a second
creation route, exactly as the system tray's is:
`cna_camera_create_with_test_backend_ext`, then `cna_camera_set_test_frame_ext` and
`cna_camera_set_test_state_ext`. That is the only way to reach a frame, or the refused and lost
states, on a machine with no camera.

**`cna_camera_create` is never called by this ABI's own test suite.** On a machine that has a camera,
opening it switches on the user's webcam and may prompt for permission — the same reason
`cna_url_launcher_open_ext` is only ever exercised through its refusals. Both gaps are recorded here
rather than papered over with a test that would misbehave on a developer's laptop.
