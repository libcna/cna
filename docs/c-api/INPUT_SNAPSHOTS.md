# CNA C API Input Snapshots and Devices

## Scope, capture and lifetime

The input boundary comprises `input.h`, `input_keyboard.h`, `input_mouse.h`,
`input_cursor.h`, `input_gamepad.h`, `input_touch.h`, `input_text.h`,
`input_haptics.h`, `input_joystick.h` and `input_devices.h` under
[`modules/c-api/include/CNA/C/`](../../modules/c-api/include/CNA/C/).

Routes taking a `game` require a live owned or callback-borrowed game handle on its creation
thread. They do not require an active lifecycle callback or a running game loop. A wrong-thread
call returns `CNA_RESULT_THREAD` when lookup is reached; input routes do not automatically
marshal themselves to another thread. See [`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

Each capture calls the corresponding native query and copies its result. The C layer adds no
frame cache, but it also does not pump platform events or force a hardware poll. SDL3's keyboard,
mouse and gamepad services maintain snapshots updated by the platform input cycle. Repeated
reads can therefore return the same state; relative mouse displacement is a consuming exception.
Guide input ownership and optional keyboard-gamepad emulation remain part of native capture.

| Representation | Lifetime and release |
|---|---|
| Keyboard, mouse, gamepad, touch, capabilities and other POD values | Caller-owned copies; remain readable after the game or callback ends |
| Joystick state | Owned snapshot handle containing variable-length arrays; destroy with `cna_joystick_state_destroy` |
| Cursor or haptic device | Owned handle; family disposal keeps the handle, family destruction releases it |
| Input event registration | Owned handle; unsubscribe with the matching family route |
| Text-event payload | Borrowed only during the callback; copy strings and arrays before returning |

POD-only helpers need no live game and may run on any thread when callers synchronize their own
memory. Handle operations retain registry thread affinity, including joystick snapshot reads.
For versioned structures, initialize the required size/version prefix as documented by the
route; a constructor-style `*_init` route writes its result. UTF-8 count/copy routes use byte
counts without a terminator and reject insufficient capacity without a partial array/string write.
A required-count output may still change on that refusal. General rules are in
[`HANDLES.md`](HANDLES.md), [`ABI_VERSIONING.md`](ABI_VERSIONING.md) and [`ERRORS.md`](ERRORS.md).

## Keyboard

The version-one snapshot contains four 64-bit words for 256 key slots. Key `N` occupies bit
`N % 64` of word `N / 64`. `input.h` names the canonical `Keys` identities, and
`CApi_KeyIdentities` compares their names and numeric values with the native header.

Key-down/up, key-state, pressed-count and ascending pressed-key copy operations inspect the
snapshot. Numeric slots 0 through 255 are accepted even without a named enum member; larger
values are invalid. Initialization from a key array, equality, hash and string routes are also
available. A copied bitfield is not restricted to physically present keys.

Live keyboard routes include the player overload, modifiers, key/scancode conversion and
name count/copy/lookups. The native player overload currently returns the shared keyboard state;
it does not select a separate physical keyboard.

## Mouse and cursors

`cna_mouse_get_state` reports logical game X/Y in absolute mode, cumulative vertical and
horizontal wheel totals, and five button bits. In relative mode X/Y are accumulated movement
deltas consumed by the native read; a second read without new movement reports zero displacement.
Snapshot construction, equality, hash and text operations remain local value operations.

The mouse family also exposes window-handle state, logical positioning, relative-mode control,
global position/warp, capture and the process-wide Clicked event. An unsupported platform operation
can fail through the exception barrier; a successful route with an `out_applied` or
`out_available` flag still requires checking that flag.

Cursor creation produces an owned Arrow description. `cna_mouse_cursor_get_stock_ext` returns
an **owned C handle** referring to one of twelve shared native singletons: every returned handle
needs destruction, while stock disposal is a no-op. Texture-based cursors copy Color or
ColorSrgbEXT pixels and validate the hot spot; the source texture need not remain alive.

Disposal clears a created cursor's description without invalidating its C handle. Setting a
disposed cursor or using an absent mouse service is a no-op. Otherwise setting a cursor delegates
to the platform and may report a platform failure. SDL3 owns its installed native cursor
independently of the C description; releasing that description does not uninstall the cursor.

## Gamepad

Player slots are the four zero-based `CNA_PLAYER_INDEX_*` identities. Default capture uses
`INDEPENDENT_AXES`; explicit capture also accepts `NONE` and `CIRCULAR`. A disconnected slot
returns a successful disconnected snapshot with rest values.

The snapshot holds connection/packet state, the canonical physical, extension and derived button
bits, two sticks and two triggers. Local button-down requires every requested bit; button-up is
its inverse. The empty mask is therefore down and not up. The C state uses one button mask;
component construction merges the supplied D-pad into that mask. Capabilities, component/state
constructors, equality, hash and string routes accompany capture.

`cna_gamepad_apply_dead_zone` is a local transform for caller-supplied analog values:

- `NONE` clamps stick axes to `[-1, 1]` and triggers to `[0, 1]`.
- `INDEPENDENT_AXES` removes/rescales each axis's dead zone, then clamps each axis.
- `CIRCULAR` removes/rescales the stick's radial dead zone and normalizes lengths above one.
- Both processed modes remove/rescale the trigger threshold before clamping.

The thresholds are `CNA_GAMEPAD_LEFT_DEAD_ZONE`, `CNA_GAMEPAD_RIGHT_DEAD_ZONE` and
`CNA_GAMEPAD_TRIGGER_THRESHOLD`. Null inputs, undefined modes and non-finite source values are
rejected without overwriting the result.

Live routes additionally expose vibration and trigger vibration, light-bar control, capabilities,
device strings, firmware/Steam identity, connection/power state, player-index control, button
labels, sensors and touchpads. Availability is separate from success: unavailable gyro,
accelerometer and touchpad-finger queries set `out_available` false and leave the reading output
unchanged. Device strings are read afresh by each size/copy call.

## Touch snapshots, collection operations and gestures

`CNA_TouchState` has eight slots; only the first `touch_count` entries are valid.
`cna_touch_get_capabilities` reports four as the XNA compatibility maximum when connected and
zero otherwise. That reported maximum does not reduce snapshot storage to four.
Connection detection includes live platform device enumeration and native touch-state fallbacks.

Native capture first returns valid `SetFinger` slots, if any. Otherwise it copies the panel's
platform-event state, ordered by touch id and limited to eight entries. Input suppression returns
an empty collection. Thus real platform touches do reach the snapshot. The C
`cna_touch_panel_raise_touch_event_ext` route invokes only the gesture dispatcher; it does not
populate that event-state store. Use `cna_touch_panel_set_finger_ext` to supply explicit slots.

Locations contain id, state, position, previous state/position and pressure. Find-by-id writes a
canonical invalid sentinel on a miss. Try-get-previous reconstructs the output even when it reports
false. Equality and hash ignore pressure; location text contains only the position.

The collection helpers mutate the caller's fixed snapshot: add, insert, remove, remove-at, clear,
contains and index-of. Add/insert into a full snapshot return `CNA_RESULT_BUFFER_TOO_SMALL`.
The native read-only property still reports true despite these exposed mutation helpers.
`copy_to` inserts at the supplied destination index and shifts existing entries, preserving CNA's
growable-vector behavior; callers supply both current destination count and capacity. Insufficient
capacity reports the required final count without changing the destination array.

`CNA_GestureType` is a bit set; use C bitwise operators and the declared mask.
`CNA_GestureSample` stores its properties as fields, including signed 100-nanosecond timestamp
ticks and CNA finger-id extensions. Reading an empty gesture queue returns
`CNA_RESULT_INVALID_STATE`; check availability first. Explicit enqueue supports synthetic samples.

TouchPanel routes control display metrics/orientation, enabled gestures, mouse-touch emulation,
window state and the device-exists flag. The C gesture dispatcher drops events while width or
height is nonpositive; otherwise it scales normalized positions/deltas and rounds to pixels.
Clearing an explicit finger slot reports its preceding active touch once as Released.
`update_ext` advances previous/frame state. The test reset clears slots, event state, gestures,
display metrics, orientation, window state and mouse-touch emulation. These are process-wide
controls; coordinate changes with the application.

## Text input and IME

Three process-wide subscriptions deliver committed UTF-16 code units, IME composition and
candidate lists. Subscription takes no game handle; the registration is released through
`cna_text_input_unsubscribe_ext`. Mouse, joystick and device events also have callbacks.

Platform UTF-8 committed text is decoded to UTF-16; a supplementary code point yields two code-unit
callbacks. The explicit committed-text raise route accepts one `uint16_t` as supplied.
Composition and candidate payloads carry UTF-8 `CNA_StringView` values borrowed only during the
callback. Composition `start`/`length` retain the platform IME's character coordinates; they
are not converted to UTF-8 byte offsets. The platform bridge normalizes an empty composition to
zero coordinates. Explicit C raise routes forward the supplied coordinates and selected-candidate
index without range checking; text/list inputs are copied and UTF-8 validated before dispatch.
Invalid text dispatches no event.

Activation uses a pair published by the native graphics layer: an opaque window handle and its
portable platform window id. `cna_text_input_set_window_handle_ext` stores the raw value and
**clears the platform id**, even when setting the same value. Setting/restoring only that value
does not restore activation. Without the published id or a text-input service, start/stop/placement
succeed as no-ops and queries return false. The explicit-window screen-keyboard query only
accepts the currently published pair; it cannot query an arbitrary window.

Start/stop/placement have void native contracts and suppress native platform failures, so success
alone does not prove activation. Query `is_active_ext` for the resulting state. Undefined
`CNA_TEXT_INPUT_TYPE_*` values are rejected with `CNA_RESULT_INVALID_ARGUMENT`.

`cna_text_input_reset_for_tests_ext` clears every subscriber and both window identifiers.
Outstanding registration handles still require unsubscription; removing an already-cleared
subscription succeeds once, then the released handle is stale. Restoring the raw handle alone
cannot restore the original window pair.

## Haptics

`input_haptics.h` maps the CNA force-feedback family. Open routes require a live game and return
an owned `CNA_HapticDeviceHandle`. Missing hardware/service can yield a valid closed device;
check `get_is_open`. A standalone open device may provide rumble without an effect-device
interface, so open state does not prove arbitrary effect support.

For valid arguments on a closed device, actions return false, effect creation returns -1, the name
is empty and capability effect limits remain -1 (unknown). Argument/handle/thread validation and
allocation/platform failures remain possible; closed state does not bypass descriptor checks.
Dispose closes native resources while keeping the handle; destroy releases it.

Custom waveform samples travel in a separate `uint16_t` pointer/count beside the POD effect.
Effect conversion validates its version, effect/direction identities and sample pointer/count,
then copies the samples. Capability names likewise travel separately through size/copy routes and
are explicit inputs to capability equality. Feature flags use C bitwise operations; capability
equality forwards their stored bits rather than universally rejecting unknown bits.
Rumble strength, gain and autocenter are forwarded without C-side clamping.

## Raw joysticks

`input_joystick.h` exposes raw axes, buttons, hats and trackballs, with device-specific ordering
and no XNA button mapping. A controller recognized as a gamepad can also appear in this raw list.
Enumeration is refreshed per call, so index-based info/name calls can observe hot-plug changes.

`cna_joysticks_capture_state` creates an owned snapshot with four variable-length arrays.
Count/copy routes read those captured arrays, and an undersized copy leaves its array unchanged.
Capture of an absent id succeeds with empty arrays. Absent capabilities report disconnected,
zero counts, unknown type/power, power percent -1 and empty name/GUID. Name/GUID reads are separate
live queries; they are explicit inputs to capability equality.

POV hat values are nine ordinal identities, not combinable direction bits: RIGHT_UP is 5.
Trackball values are relative deltas. With SDL3, native polling consumes the hardware deltas and
stores them in the platform snapshot; repeated C captures copy that stored value without consuming
it again. This differs from relative mouse reads.

Connected/disconnected subscriptions are process-wide and need no game. Raise/reset routes do
take a game. Reset clears subscribers without releasing their registration handles; unsubscribe
each handle afterwards.

## Host sensors, device enumeration, clipboard and power

`input_devices.h` exposes host accelerometer/gyroscope readings, sensor enumeration, mouse/
keyboard/touch-device metadata, text clipboard operations and power. Host sensor absence is
successful with `out_available = false`, leaving the reading untouched. These sensors are
separate from the controller sensor routes.

Device lists are metadata only: a listed id does not select a separate XNA keyboard or mouse
state. `CNA_InputDeviceInfo` uses a 64-bit id to accommodate touch devices. Each index-based
query re-enumerates, and names remain separate UTF-8 size/copy results. Four mouse/keyboard
hot-plug events use process-wide subscriptions and family unsubscription.

Clipboard set validates the text and requests the native void operation. Missing service and
suppressed platform write failures can still return success; readback is a separate observation,
and another process can change the clipboard between size/copy calls. This C family exposes text;
native primary-selection and MIME-data additions are not implied by it.

Power reports state, seconds remaining and percent. Unknown/Error are result values, and -1
means an unknown duration or percentage.

## Validation and scope boundary

The strict-C tests are `CApi_InputSnapshotsSmoke`, `CApi_HapticsSmoke`,
`CApi_JoystickSmoke` and `CApi_InputDevicesSmoke`. They cover local values, malformed inputs,
event dispatch/lifetime, handle/thread failures and live-query or absent-device outcomes.
Key/button identity checks and C17/C++23 header builds cover the ABI declarations.

The current audit runs these C tests through the private SDLGPU runner. The text smoke restores
only a raw handle after reset and accepts an inactive result; it does not prove real IME activation.
Native `TextInputEXTTest` cases separately exercise published handle/id pairs through a test
platform. Physical gamepad, touch, sensor and force-feedback behavior requires the hardware/platform
evidence in [`input-manual-verification-results.md`](../input-manual-verification-results.md).

C API coverage is tracked in [`COVERAGE.md`](COVERAGE.md); the input module still has planned
rows. A mapped declaration or passing absent-device smoke does not establish complete hardware
support. Native behavior and platform limits are in [`input-backend.md`](../input-backend.md) and
[`input-fna-fidelity.md`](../input-fna-fidelity.md).
