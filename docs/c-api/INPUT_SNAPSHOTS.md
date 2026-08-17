# CNA C API Input Snapshots

## Capture model

The keyboard, mouse, gamepad and touch capture functions perform one canonical CNA query at the
call site and copy the result into a caller-owned structure. The required game handle establishes
the active runtime and creation-thread boundary. A returned snapshot is not a borrowed native
object and remains usable after its callback or game ends.

Capture is point-in-time, not frame-cached. Two calls during one update or draw callback perform
two separate native captures and may observe different state if the platform input bridge changes
between them. Every capture must run on the game's creation/graphics thread and returns
`CNA_RESULT_THREAD` otherwise. The local query and normalization helpers inspect only copied POD
values and may run on any thread.

## Keyboard

The version 1 snapshot contains four 64-bit words covering the same 256 numeric key slots as FNA's
keyboard bitfield. Key `N` occupies bit `N % 64` of word `N / 64`. `input.h` names every one of the
160 canonical `Keys` identities; a build/CTest audit compares the complete C list and every numeric
value directly with CNA's native `Keys.hpp`.

`is_key_down`, `is_key_up`, pressed-count and ascending count/copy functions inspect only the POD
snapshot. They do not call CNA, SDL or a platform backend, so a copied snapshot may be queried from
any thread. Key slots 0 through 255 are valid even where the sparse XNA enum has no named value;
unnamed slots from a native capture remain clear. Values above 255 are invalid arguments. An
undersized pressed-key destination receives the required count and no partial array.

## Mouse

`cna_mouse_get_state` copies logical X/Y coordinates, cumulative vertical and CNA horizontal wheel
values, and five button states into `CNA_MouseState`. Pressed buttons use the stable
`CNA_MOUSE_BUTTON_*` bit set. The snapshot contains no live cursor/window pointer and has no
post-capture lifetime restriction.

## Gamepad

Player slots use the four zero-based `CNA_PLAYER_INDEX_*` identities. `cna_gamepad_get_state`
matches CNA's ordinary `GamePad::GetState(PlayerIndex)` overload and therefore applies
`CNA_GAMEPAD_DEAD_ZONE_INDEPENDENT_AXES`. The explicit overload accepts `NONE`,
`INDEPENDENT_AXES` or `CIRCULAR`. A disconnected slot is a successful snapshot with
`is_connected == CNA_FALSE` and rest values; disconnection is not an API error.

The snapshot carries connection and packet information, all 31 currently canonical physical,
extension and derived gamepad button bits, two normalized thumbsticks and two normalized triggers.
Combined local button queries match CNA: down is true only when every requested bit is present;
up is its inverse. Consequently the empty mask is down and not up.

`cna_gamepad_apply_dead_zone` exposes the same deterministic transform for synthetic or previously
stored analog input without querying a device:

- `NONE` square-clamps stick axes to `[-1, 1]` and triggers to `[0, 1]`;
- `INDEPENDENT_AXES` excludes `7849/32768` from each left-stick axis and `8689/32768` from each
  right-stick axis, rescales the remainder, then square-clamps it;
- `CIRCULAR` excludes and rescales the length of each whole stick, then normalizes lengths above
  one;
- both processed modes exclude and rescale the trigger threshold `30/255` before clamping.

Null pointers, invalid modes and non-finite input are rejected without changing the destination.

## Touch

`cna_touch_get_capabilities` copies current device connection and CNA's reported maximum touch
count. `cna_touch_get_state` copies a collection into a fixed eight-entry `CNA_TouchState`; only
the first `touch_count` entries are valid. CNA currently reports a maximum of four when a device
is connected, while the eight-entry ABI capacity preserves the native `TouchPanel` storage bound.
Platform absence is represented by disconnected/empty successful snapshots.

Each `CNA_TouchLocation` carries its id, state, logical-pixel position, optional previous state and
position, and CNA's pressure extension in `[0, 1]` when the platform supplies it. The local
find-by-id helper always writes either the first match or the canonical invalid sentinel. The
previous-location helper always reconstructs an output location and reports false when its state
is invalid, matching CNA's out-parameter behavior.

## Text input

Text input is the one input family that is event-driven rather than sampled, so it is the one that
carries callbacks. `input_text.h` maps all three canonical events —
committed text, IME composition and the IME candidate list — as owned
`CNA_TextInputRegistrationHandle` values. The canonical events are process-wide statics, so a
subscription takes no game handle; one `cna_text_input_unsubscribe_ext` releases all three kinds,
because a registration already knows which event it came from.

A committed code unit crosses as a `uint16_t`. A code point above U+FFFF arrives as two calls, a
high surrogate then a low surrogate, exactly as the canonical event delivers it. The two multi-field
events hand over a versioned `CNA_TextEditingEventInfo` or `CNA_TextEditingCandidatesEventInfo`
whose UTF-8 text and candidate strings are `CNA_StringView`s **borrowed only for the duration of the
callback**; copy them before returning. Composition `start`/`length` are byte offsets into
multi-byte text, forwarded verbatim and deliberately not range-checked, because the canonical
dispatch does not check them either.

The three `cna_text_input_raise_*_ext` routes map the canonical internal dispatchers, which is what
makes the events observable without a real keyboard. Input text is copied and UTF-8 validated before
dispatch; a refusal dispatches nothing.

The native window is an opaque `uint64_t` the C API never dereferences, and only zero is guarded —
a nonzero value must be a live window published by the platform layer. A backend that creates a real
window publishes one automatically, and there activation genuinely takes effect; with no window
bound, every activation route is a successful no-op and every query answers false. Do not infer
which case applies from the compiled-in renderer: query the handle, or observe the answers.

An undefined `CNA_TEXT_INPUT_TYPE_*` identity is refused. This is a deliberate deviation: the
canonical conversion silently falls back to plain text, so a C consumer would otherwise get a
different keyboard than it asked for without being told.

`cna_text_input_reset_for_tests_ext` maps the canonical test-support reset. It clears the bound
window handle and drops every subscription to all three events, including registrations this API
handed out, so releasing one afterwards is a no-op rather than a failure. It changes process-wide
state a caller may not own; restore the window handle afterwards.

## Touch and gestures

`input_touch.h` completes the touch family. It **extends** the fixed eight-slot `CNA_TouchState`
snapshot the C API already had rather than introducing a second representation: the canonical
`TouchCollection` mutation surface — add, insert, remove, remove-at, clear, contains, index-of and
the copy — operates on that snapshot in place. Its capacity is exactly the canonical touch-panel
maximum, and an append or insert past it is refused rather than silently dropping a touch.

Three canonical value behaviors are reproduced rather than tidied up. Equality and the hash ignore
the pressure extension, so two locations differing only in pressure are the same location and the
collection's search and removal treat them as such. The text carries **only the position**, as
`{Position:{X:… Y:…}}`. And the copy **inserts** at its index and shifts what is already there,
because the canonical destination is a growable vector — which is why the destination's current
element count is an argument in C rather than being inferred.

`CNA_GestureType` is a real bit set, so the canonical flag operators need no route: C composes and
masks it with its own operators, and every route validates against `CNA_GESTURE_TYPE_ALL`.
`CNA_GestureSample` is a fixed 64-byte value whose eight canonical getters are plain fields;
`System::TimeSpan` crosses as `int64_t` 100-nanosecond ticks, the same spelling `runtime.h` and
`audio.h` already use.

Every `TouchPanel` static takes an active game handle, for the same reason the gamepad and mouse
queries do. Four of them behave in ways worth stating plainly:

- **Reading an empty gesture queue is a refusal**, `CNA_RESULT_INVALID_STATE`, because the canonical
  read throws. Check `cna_touch_panel_get_is_gesture_available` first. `..._enqueue_gesture_ext` is
  what makes the queue observable with no touch device at all.
- **A raised touch event feeds gesture detection, not the snapshot.** The slot array
  `cna_touch_get_state` reports is populated by `cna_touch_panel_set_finger_ext`; the two are
  separate sources. A raised event is also **dropped entirely** while no display size is published,
  because the canonical dispatch scales normalized coordinates by that size.
- **Clearing a slot does not make a touch vanish.** The next frame reports it once more as released
  with its previous state carried over, which is the XNA contract for a lifted finger.
- **The reset clears the display metrics and the window handle too.** The canonical class comment
  says they survive; the implementation clears them deliberately, so a leaked display size cannot
  corrupt another test's scaled coordinates. This contract follows the behavior.

The display size, orientation, enabled gestures, window handle and device-exists flag are all
process-wide state. Restore what you change. As with text input, do not assume the window handle
starts at zero — a windowed backend publishes a real one.

## Haptics

`input_haptics.h` maps the `CNA::Input` force-feedback family. It is the only input family with no
XNA counterpart at all, so — following `core_ext.h` — the whole header maps a CNA-namespace surface
and its routes carry no `_ext` suffix. It is also the only input family whose canonical type is an
owned resource rather than a value: `HapticDevice` becomes an owned `CNA_HapticDeviceHandle`, with
the same destructor/`Dispose` split `MouseCursor` uses, so a caller can close a device without
giving up its handle.

**A closed device is not an error state.** `cna_haptics_open`, `..._open_from_joystick` and
`..._open_from_mouse` never fail for want of hardware: each hands back a real handle, and
`cna_haptic_device_get_is_open` reports whether anything is behind it. Every other route on a closed
device succeeds and answers `CNA_FALSE`, zero or -1 through its output. That is what makes the
family usable — and testable — on a machine with no force-feedback hardware, which is the normal
case.

Two representational decisions are worth knowing before using the header:

- **A custom waveform travels beside the effect, not inside it.** The canonical descriptor owns its
  sample vector; `CNA_HapticEffect` does not, so it stays a plain copyable POD. Every route that
  takes an effect takes a `custom_data` pointer and a sample count alongside it.
- **The device name is not part of the capability value.** A string does not belong in a POD, so
  read it with `cna_haptic_device_get_name_size`/`_copy_name`. That is why
  `cna_haptic_capabilities_equals` takes both names as arguments: it reproduces the canonical
  comparison exactly rather than quietly comparing fewer fields than the canonical operator does.

Canonical behavior preserved rather than tidied up: rumble strength, gain and autocenter are handed
to the platform unvalidated; freeing an unknown effect identifier is a successful no-op, because the
canonical operation reports nothing; and a closed device's capability value keeps `max_effects` and
`max_effects_playing` at **-1**, which means "unknown" and is deliberately distinguishable from
"none". `CNA_HapticFeature` is a real bit set, so its five canonical operators need no route — C
composes them itself, and every route validates against `CNA_HAPTIC_FEATURE_ALL`.

No `SDL_Haptic` ever crosses the ABI. The device enumeration is a point-in-time snapshot taken by
each call, so an index from `cna_haptics_get_count` is only valid until the device set changes.

## Raw joysticks

`input_joystick.h` maps the `CNA::Input` raw-joystick family — flight sticks, wheels, throttles and
arbitrary HID controllers that `GamePad` cannot represent. Like haptics it is a CNA-namespace
surface, so its routes carry no `_ext` suffix except the two hot-plug events and the test reset,
whose names follow the canonical members. Everything here is raw and unmapped: axis order, button
numbering and hat count are whatever the hardware reports, with no XNA semantic assignment. A device
the platform also maps as a gamepad appears here too, as `CNA_JOYSTICK_TYPE_GAMEPAD` — an
independent view of the same hardware, not a second copy of `GamePad`.

**A snapshot is an owned handle, not a value.** This is the one place the input families depart from
the fixed-POD rule, and deliberately. The canonical `JoystickStateEXT` carries four heterogeneous
variable-length arrays — axes, buttons, hats and trackballs — with no canonical maximum, unlike the
touch panel's fixed eight slots. A fixed value would have to invent a capacity, and any capacity
small enough to be reasonable silently truncates a real HOTAS setup. Four independent per-array
queries would avoid that but would answer from four different instants. So
`cna_joysticks_capture_state` captures once into a `CNA_JoystickStateHandle`, and each array is read
with its own count/copy pair against that one instant. Release it with `cna_joystick_state_destroy`.

**A hat is an identity, not a bit set.** The platform encodes a POV hat as an up/down bit combined
with a left/right bit, but the canonical enumeration lists the nine reachable combinations, and so
does `CNA_JoystickHatPosition`: `CNA_JOYSTICK_HAT_POSITION_RIGHT_UP` is 5, not `RIGHT | UP`. Do not
compose these values.

**An absent device is an ordinary answer, not a failure** — the same contract haptics established.
`cna_joysticks_get_capabilities` on an identifier that is not connected succeeds with
`is_connected` false, zero counts, an unknown type and power state, a power percent of **-1**
("unknown", deliberately distinguishable from an empty battery) and two empty strings;
`cna_joysticks_capture_state` succeeds with four empty arrays. As with haptics, that is what makes
the family testable on a machine with no joystick, which is the normal case.

Two strings stay outside the capability value, for the reason a string never belongs in a POD: the
device name and the device GUID are read by identifier through
`cna_joysticks_get_capabilities_name_size`/`_copy_capabilities_name` and the matching GUID pair, and
`cna_joystick_capabilities_equals` therefore takes both strings alongside both values so it
reproduces the canonical ten-field comparison exactly. The GUID is the canonical lowercase
hexadecimal text, not a binary value. The enumerated descriptor works the same way:
`CNA_JoystickInfo` carries the identifier and type, and the name comes from
`cna_joysticks_get_name_size_at`/`_copy_name_at`.

The two hot-plug events are process-wide static fields, exactly like the mouse and text-input
events, so their subscriptions take no game handle. One shared `cna_joysticks_unsubscribe_ext`
releases either kind, `cna_joysticks_raise_connected_ext`/`_raise_disconnected_ext` invoke the same
public field the platform layer invokes, and `cna_joysticks_reset_for_tests_ext` clears every
subscriber — process-wide state a caller does not own alone. Releasing a registration after that
reset removes nothing rather than failing.

No `SDL_Joystick` ever crosses the ABI. The enumeration reports the devices the native layer
currently holds open and is a point-in-time snapshot taken by each call, so an index from
`cna_joysticks_get_count` is only valid until the device set changes. Trackball values are relative
motion since the previous read, so capturing consumes them.

## Host sensors, device enumeration, clipboard and power

`input_devices.h` maps the last four `CNA::Input` extensions, all of them small host-system
queries: the machine's own motion sensors, the connected-device lists, the system clipboard and the
battery state. None has an XNA counterpart, so the routes take no `_ext` suffix except the hot-plug
events and the test reset.

**Availability is separate from the answer, and the answer is left alone.**
`cna_sensors_get_accelerometer` and `cna_sensors_get_gyroscope` report through an `out_available`
flag; having no sensor is an ordinary success, not a failure. When the flag is false the reading
output is **left exactly as the caller left it**, because the canonical query leaves its reference
untouched — so a caller that pre-fills a value keeps it. These are the machine's own sensors; a
controller's are `cna_gamepad_get_gyro_ext` and `cna_gamepad_get_accelerometer_ext`.

The three device lists — mice, keyboards, touch devices — are the same index-addressed enumeration
the joystick and haptic families use: `cna_input_devices_get_mouse_count`, `_get_mouse_info_at`,
`_get_mouse_name_size_at`, `_copy_mouse_name_at`, and the keyboard and touch equivalents. They are
**metadata only**, exactly as the canonical class documents: XNA input state stays merged across
devices, so an identifier here does not select a device to read from. `CNA_InputDeviceInfo` carries
a `uint64_t` identifier, wider than the sensor and joystick descriptors, because a touch-device
identifier is 64-bit natively.

The four hot-plug events work like the joystick pair: process-wide static fields, so their
subscriptions take no game handle, one shared `cna_input_devices_unsubscribe_ext` releases any of
them, each has a raise route, and `cna_input_devices_reset_for_tests_ext` clears all four at once.

**The clipboard's setter reports that the request was made, not that it succeeded.** The canonical
operation returns nothing, so there is no platform outcome to forward, and this ABI does not invent
one: `cna_clipboard_set_text` succeeds even where the platform ignores it — a headless session with
no clipboard, or a browser that requires a user gesture. Read it back with
`cna_clipboard_get_text_size`/`cna_clipboard_copy_text` if the outcome matters. The clipboard is
also process-external state that another application can change between two calls, so treat the size
as a hint and always use the byte count the copy itself reports.

`cna_power_get_info` returns the same `CNA_PowerState` identity the gamepad power query uses and
always writes all three outputs. `CNA_POWER_STATE_UNKNOWN` and `CNA_POWER_STATE_ERROR` are canonical
answers rather than C failures, and -1 in the remaining-seconds or percent output means "unknown"
rather than "none left".

## Current scope boundary

Apart from the text-input, joystick and device hot-plug events above, the input families
deliberately expose no live native state pointer or per-key platform call. Every public `input`
declaration now has a C mapping, so the module's share of the coverage matrix is closed.
