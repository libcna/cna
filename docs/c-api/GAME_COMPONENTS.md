# Game runtime, components, window and services in the C API

The game-runtime boundary is split across four public headers:

- `CNA/C/runtime.h` — game creation, lifecycle, frame driving, properties, events, launch parameters,
  framework dispatch and title files;
- `CNA/C/runtime_components.h` — C-authored components, the game's component collection and named
  service queries;
- `CNA/C/runtime_window.h` — the game-owned window, events and native-window interop;
- `CNA/C/runtime_graphics_manager.h` — `GraphicsDeviceManager`, device preferences and its events.

The game's borrowed content manager is declared in `CNA/C/content.h`. Cross-cutting callback,
threading, re-entrancy and shutdown rules are defined by
[CALLBACKS_AND_THREADING.md](CALLBACKS_AND_THREADING.md); this page records the runtime-specific
contract.

## Game creation and callback behavior

`cna_game_create` creates the process's C-owned `Game` from a versioned `CNA_GameCreateInfo`. The
configuration selects fixed or variable timing, a positive target step in 100-nanosecond ticks, an
initial UTF-8 title and an optional `CNA_GameCallbacks` table. The table is copied, but its function
pointers and context must remain valid until the game is destroyed.

The five main callbacks are load, update, draw, unload and exiting. A callback receives a borrowed
game handle valid only for that callback. Returning anything other than `CNA_RESULT_SUCCESS` stops
the run at a safe point and makes the enclosing route report `CNA_RESULT_CALLBACK`; an optional
`CNA_CallbackError` message is copied before the callback returns. Observer events registered with
`cna_game_subscribe` return `void` and cannot fail the run.

`CNA_GameFrameHooks` is deliberately a second versioned table. It supplies initialize, begin-run,
end-run, begin-draw and end-draw without extending the already-published positional initializer for
`CNA_GameCallbacks`. A begin-draw hook may clear `out_should_draw` to skip that draw. Passing null to
`cna_game_set_frame_hooks_ext` removes all frame hooks.

The first run delivers initialization before content loading, then begin-run, updates and draws.
Lifecycle callbacks, frame hooks, component callbacks and activated/deactivated event handlers all
run inside the same callback scope on the game thread.

## Driving the game and thread ownership

The C API supports three host shapes:

- `cna_game_run` owns the blocking loop until exit or callback failure;
- `cna_game_run_one_frame` advances one native frame;
- `cna_game_run_frame_ext` advances a host-owned run one frame at a time, including begin-run and
  terminal exiting/end-run delivery. This is the browser/request-animation-frame form.

`cna_game_tick` exposes the canonical update/draw step below host event processing. Recursive game
driving and destruction are refused with `CNA_RESULT_INVALID_STATE` while a lifecycle, frame,
component or activation callback is active. Callback-safe state changes include requesting exit,
clearing the target, changing the window title, resetting elapsed time and suppressing the next draw.

Handles are owner-thread objects by default. `cna_game_set_foreign_thread_calls_ext` can opt a game
into queueing qualifying foreign-thread handle calls for execution on its game thread. The caller
blocks for the result and diagnostic. The queue is pumped at update/draw boundaries or explicitly by
`cna_game_run_foreign_thread_calls_ext`; game-driving, queue-configuration/pump and destroy routes
themselves remain game-thread-only. The full queue and shutdown rules are in
[CALLBACKS_AND_THREADING.md](CALLBACKS_AND_THREADING.md#opt-in-foreign-thread-service).

## Game properties, events and borrowed graphics device

The game handle exposes focus, mouse visibility, fixed/variable timing, target step, inactive sleep,
derived FPS/frame duration, the raw run flag and the canonical type name. Durations use
100-nanosecond ticks. `cna_game_reset_elapsed_time` discards accumulated delay, and
`cna_game_suppress_draw` skips the next draw without skipping its update.

The four `CNA_GameEvent` identities cover activated, deactivated, disposed and exiting. Their
registration handles are released with `cna_game_unsubscribe`. Game and window registrations do not
block game destruction: the source invalidates them after its final events, and the caller still
releases each registration handle once.

`cna_game_get_graphics_device` returns a borrowed device during callback scope, or before the first
run from the owned game handle. The loan ends at the documented callback boundary and has no destroy
route. Resources created through the loan receive independent owned handles. A drawable component
and `GraphicsDeviceManager` expose the same game-owned device under the same borrowing rules; see
[RENDERERS_AND_CAPABILITIES.md](RENDERERS_AND_CAPABILITIES.md#borrowed-graphics-device-handle).

## Launch parameters

The launch-parameter map belongs to the game and has no separate handle. Keyed routes query values;
the key size/copy pair enumerates names in ascending ordinal byte order so indices remain
deterministic for an unchanged key set.

`cna_game_launch_parameters_parse_ext` replaces the map. It trims leading flag markers and splits on
the first colon, not `=`. A nonempty colonless argument is retained with an empty value, a trailing
colon also yields an empty value, and an empty name is ignored. The first parsed occurrence wins.
An empty argument list clears the map rather than re-reading the process command line.

`cna_game_launch_parameters_add` follows `Dictionary::Add`: a duplicate key returns
`CNA_RESULT_INVALID_ARGUMENT` and leaves the original value unchanged. Empty direct keys and values
are valid.

## Framework dispatch and title files

`cna_framework_dispatcher_update` pumps the framework-wide work normally performed by the game loop.
It takes a game only for thread affinity and is harmless, though redundant, while the loop is
already running.

The title base path is process-wide, not game-local. `cna_title_location_set_path_ext` changes every
later title read. `cna_title_container_read_ext` deliberately narrows the canonical open stream to a
whole-file size/copy route: insufficient capacity returns `CNA_RESULT_BUFFER_TOO_SMALL` without a
partial write. Empty, rooted, escaping and otherwise invalid cleaned names return
`CNA_RESULT_INVALID_ARGUMENT`; a missing or unreadable entry returns `CNA_RESULT_IO`.

## C-authored game components

Canonical `IGameComponent`, `IUpdateable` and `IDrawable` behavior cannot be implemented directly in
C. `CNA_GameComponentCallbacks` supplies initialize, update, draw, load-content, unload-content and
dispose functions plus a context; the C++ adapter implements the interfaces and forwards each step.
Every member may be null. `cna_game_component_create` creates an updateable component, while
`cna_drawable_game_component_create` also maps drawing and the protected content hooks.

Creation does not add a component to the game. Once added, the game initializes it and drives each
enabled update and visible draw. The explicit public update and draw routes do not consult those
flags. Calling `cna_game_component_initialize` invokes the C initialize override each time; a
drawable component's base content load still happens only on its first initialization.

Canonical details intentionally preserved at the boundary include:

- `CompareTo` returns the other update order minus this one, so the earlier component compares
  greater;
- setters raise their change event only when the value changes;
- drawing properties and events on a non-drawable component return `CNA_RESULT_INVALID_STATE`;
- every explicit `cna_game_component_dispose` invokes the dispose callback and disposed event;
- a drawable disposal also invokes unload-content every time, even before initialization;
- disposal removes collection membership before the disposed event but leaves the handle valid.

Consumer callbacks must make their own resource release idempotent. Repeated explicit disposal is
observable and is not a no-op.

## Component collection and ownership

A game owns one component collection, so collection routes take the game rather than a collection
handle. Add and insert reject duplicate membership with `CNA_RESULT_INVALID_ARGUMENT`; adding after
game initialization initializes the component immediately. Remove and clear end membership and emit
removal events without disposing or releasing the component, so a removed handle can be added again.
`IndexOf` returns `-1` for absence.

The canonical collection stores interface pointers. CNA retains C-created components and maps those
pointers back to handles for indexing and collection events. A native component with no C handle is
reported as `CNA_INVALID_HANDLE`. Destroying a C component first removes any remaining membership,
then invalidates its handle; otherwise the native collection would retain a released pointer. Live
component handles block game destruction.

The component's protected load/unload hooks are representable because this ABI supplies the derived
adapter. Protected `GameWindow` hooks are not: the C API does not supply a derived window. The rule
is whether a C-owned adapter exists, not visibility alone.

## Named game services

`GameServiceContainer` is keyed by C++ type identity, which C cannot express generically. The C API
therefore names only the runtime services it can identify with `CNA_GameServiceType`:

- `CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER`;
- `CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE`.

`cna_game_services_contains_ext` queries those types and `cna_game_services_remove_ext` removes one;
removing an absent service is a successful no-op. Generic registration has no C form because C
cannot supply the C++ service type or implementation. C-owned application services belong in the
callback context rather than a parallel, noncanonical registry.

## The game window

A game owns one `GameWindow`, so every `cna_game_window_*` route addresses the game handle. The
surface exposes client bounds (including desktop `x`/`y` where the platform can report them),
orientation, title, screen-device name, resize permission, borderless/minimize/restore requests,
screen-device changes, the type name and three window events. Window subscriptions use the common
`cna_game_unsubscribe` route.

The two platform accessors are intentionally different:

- `cna_game_window_get_native_handle_ext` returns XNA's integer `Handle` round-trip token. It is for
  `PresentationParameters.DeviceWindowHandle`, not general native interop.
- `cna_game_window_get_native_window_ext` fills a versioned `CNA_NativeWindowHandle` with the
  window-system identity and borrowed Win32, X11, Wayland, Cocoa, Android or UIKit fields. Web,
  headless and terminal identities carry no native pointer.

Native pointers remain platform-owned and are valid only while the game window lives. A platform
with no native window is a successful answer, not a failed query.

Window changes are platform requests. A platform that cannot honor a real-window change returns
`CNA_RESULT_PLATFORM`; a renderer with no native window accepts the request as a no-op. The single
end-screen-device-change route represents both canonical overloads: non-positive width or height
keeps the current dimension.

## The graphics device manager

`cna_graphics_device_manager_create` takes the game used by the canonical constructor, registers the
manager under both named graphics services and rejects a second manager. It exposes profile,
fullscreen, MSAA, back-buffer size and format, depth/stencil format, vertical synchronization,
supported orientations and CNA's presentation-scaling mode. Setters record preferences;
`cna_graphics_device_manager_apply_changes` negotiates and applies them through device reset and
window reconfiguration. Renderer capability and applied values may differ from the preference.

The complete native behavior, current defects and renderer-independent evidence are maintained in
[gdm-coverage.md](../gdm-coverage.md). In particular, this C surface preserves the native manager's
current width/height validation behavior rather than adding a binding-only rule.

`cna_graphics_device_manager_dispose` is idempotent, unregisters both services and raises disposed
once while leaving the handle valid. Destroy invalidates the handle but retains the disposed native
manager object until its game dies: `Game` caches a raw graphics-service pointer, so freeing the
manager earlier would leave a dangling pointer. The game-owned graphics device itself is not
destroyed with the manager.

Data-free manager events and the observation-only preparing-settings callback return registration
handles released by `cna_game_unsubscribe`. The `_ext` preparing-settings subscription receives a
mutable `CNA_GraphicsDeviceInformation`; valid changes are copied back, while an invalid returned
structure is ignored atomically. The adapter index replaces the native pointer-valued adapter field.

The C API does not let a caller implement `IGraphicsDeviceManager`: the runtime constructs and
resolves its concrete manager through the service container. This differs from game components,
where the callback adapter is the supported C implementation seam.

## The game's borrowed content manager

`cna_game_get_content_manager_ext` returns the same borrowed handle to the game's value-member
content manager on every call. All ordinary content-manager operations accept it, but
`cna_content_manager_destroy` returns `CNA_RESULT_INVALID_STATE`; the game invalidates the handle on
destruction. It therefore does not block game destruction as an independently owned manager would.

`cna_game_set_content_manager_ext` follows the canonical reference setter and copies the source
manager. Later source changes do not affect the game, and the borrowed handle continues to address
the game's own copy. The complete manager and resource ownership rules are in
[CONTENT.md](CONTENT.md).

## Deliberately absent or historical shapes

- Generic C++ service registration and caller-implemented `IGraphicsDeviceManager` have no C form.
- Protected window hooks have no C-derived window on which to operate.
- Title content is available as a whole-file copy, not as the canonical open stream.
- The retired declaration-only `CNA::Runtime`/`RuntimeOptions` C++ facade is not a C API feature.
  Its removal and the still-valid `CNA::Runtime` CMake target are documented in
  [physical-modules.md](../physical-modules.md); `CApi_RetiredRuntimeFacade` keeps the old symbols
  absent.

## Verification boundary

`CApi_RuntimeGameSmoke`, `CApi_HostDrivenRunSmoke`, `CApi_RuntimeComponentsSmoke`,
`CApi_RuntimeComponentDeviceBorrow`, `CApi_GraphicsDeviceManagerSmoke`,
`CApi_ForeignThreadCallsSmoke`, `CApi_OwnerThreadRuntime`, `CApi_StressSmoke`, the ABI tests and the
generated-route smoke cover this boundary. Platform-conditional window operations accept and test
both success and `CNA_RESULT_PLATFORM`; platform-specific native-window qualification remains a
platform responsibility rather than being inferred from one strict-C configuration.
