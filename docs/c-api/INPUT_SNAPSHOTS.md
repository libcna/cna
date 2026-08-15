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

## Current scope boundary

Apart from the text-input events above, the input families deliberately expose no live native state
pointer, per-key platform call or device event subscription. The haptics family and the remaining
`CNA::Input` joystick, sensor, clipboard, power and device-enumeration extensions remain planned
for complete-public-API coverage.
