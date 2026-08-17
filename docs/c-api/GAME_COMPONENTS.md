# Game components and services

## A component is behavior the caller supplies

Everywhere else in this ABI a caller consumes canonical behavior. A component is the exception: the
canonical types are C++ interfaces — `IGameComponent`, `IUpdateable`, `IDrawable` — and **C cannot
implement an interface**. So a component in C is a `CNA_GameComponentCallbacks` set plus a context,
and this ABI supplies the object that implements the interfaces and forwards each lifecycle step to
it. `cna_game_component_create` makes an updateable one; `cna_drawable_game_component_create` makes
one that also draws.

Every callback member may be null, and a null member is simply not called. That is how a component
opts out of a step, rather than by implementing an empty one.

Because this ABI supplies the derived class, the canonical **protected** content hooks
`LoadContent` and `UnloadContent` are callback members like any other. This is the opposite of what
the sensor family does with its protected members, and the difference is exactly that: a sensor had
no derived class to hang them on, so C could not reach them; a component does.

## Ownership and the collection

Creating a component does not add it to the game. It exists on its own until `cna_game_components_add`
puts it in the collection the game drives — the same two steps a C++ caller takes.

A game owns exactly one component collection and one service container, so neither has a handle of
its own: every collection route addresses the game's. The canonical collection stores raw interface
pointers, so this ABI keeps its own map from those pointers back to handles; that is what lets
`cna_game_components_get_at` and both collection events answer in handles rather than in addresses a
C consumer could do nothing with. A component the collection holds but this ABI did not create
answers as an invalid handle, because there is no handle to give.

**Releasing a component removes it from the collection first.** The canonical destructor does not do
that — a C++ caller manages membership itself — but a handle-based ABI must never leave the runtime
holding a pointer to something it has released. For the same reason a game refuses to be destroyed
while any component handle is still alive, exactly as it already does for graphics resources, content
managers and audio resources.

## Canonical behaviors reported rather than corrected

- **The comparison is inverted.** `CompareTo` subtracts this component's update order from the
  other's, so a component that updates *earlier* compares *greater*. It is preserved as it is.
- **A second disposal is a no-op**, not a refusal — the opposite of what a sensor does, and this
  type's own idempotence.
- Adding the same component twice is allowed, because the canonical collection allows it.
- A component that is not in the collection answers **-1**, the canonical sentinel, rather than a
  separate presence flag.
- Every drawing route on a component that does not draw reports `CNA_RESULT_INVALID_STATE` rather
  than answering a default.

## The service container is the one thing C cannot fully have

`GameServiceContainer` is keyed by **C++ type identity**. C cannot name a type, and cannot author an
object implementing a C++ interface to register under one. So:

- Looking up and removing are exposed as `cna_game_services_contains_ext` and
  `cna_game_services_remove_ext`, keyed by a `CNA_GameServiceType` identity naming the services the
  runtime itself registers — today the graphics device manager and the graphics device service. That
  is a **subset** of the canonical operation, and the coverage inventory records it as one.
- **Registering has no C form at all.** A C consumer's own services belong in the context pointer
  every callback in this ABI already carries. Inventing a parallel key space would make the container
  mean something different from the canonical one.

Removing is permitted because the canonical operation permits it, and it cannot be undone from C.
The game keeps working off the pointers it resolved while initializing, so what a removal changes is
what a **later** lookup finds.

## The game's own surface

The game handle carries the rest of the runtime: focus, mouse visibility, fixed or variable timing,
the target step and the inactive sleep, the derived frame rate and frame time, the run switch, the
type name, and the four game events through `cna_game_subscribe`. Durations are 100-nanosecond ticks
like every other duration in this ABI.

**A frame step is refused from inside a lifecycle callback.** `cna_game_tick` joins running and
destroying the game in that rule, for the same reason: a frame step called from within a frame
re-enters the loop it is part of. Setting a flag — suppressing the next draw, forgetting the
accumulated time — is fine from anywhere.

### Frame hooks are a second table, deliberately

The canonical `Game` has five more lifecycle hooks than `CNA_GameCallbacks` carries: initialization,
begin and end of a run, and begin and end of each draw. They arrive as `CNA_GameFrameHooks`,
installed with `cna_game_set_frame_hooks_ext`, rather than as new members on the published callback
table.

That is a design decision worth naming. Appending members to `CNA_GameCallbacks` would have been
ABI-safe — the structure is size-prefixed exactly so it can grow — but it would have left every
positional initializer any consumer has already written incomplete, which a compiler warns about and
a strict build rejects. A separate table costs one extra call at startup and breaks nothing that
exists. The pre-draw hook answers a boolean like the canonical one: clear the flag and the frame's
drawing is skipped.

### Launch parameters keep their canonical parsing

The parser is the canonical one, not the one a reader expects: names and values split on the first
**colon**, not an equals sign; leading flag markers are trimmed; an argument shorter than three
characters or without a colon is skipped in silence; and the first occurrence of a name wins. Parsing
an empty list clears the parameters rather than re-reading the command line.

### Title content is read whole

`cna_title_container_read_ext` delivers a **whole file** where the canonical operation hands back an
open stream. This ABI has no stream handle for title content, and a title asset is read to be used;
incremental reads are the deliberate omission. The title path is process-wide, exactly as
canonically, and a missing file reports `CNA_RESULT_IO` rather than surfacing the canonical plain
runtime error as an internal failure.

## The window, and the one-per-game question

Every `cna_game_window_*` route addresses the game handle. That is the **fourth** time this ABI has
answered the same question the same way — after the display metrics, the component collection and the
service container — and the reason is always the reason: a game owns exactly one of these, so a
handle would add a lifetime to track and nothing to gain.

Two canonical shapes collapse into one route each. The platform-handle property and the
native-window accessor answer the same pointer, so there is one
`cna_game_window_get_native_handle_ext`. The name-only screen-device-change overload is the sized one
with the current client size, so there is one `cna_game_window_end_screen_device_change` and a
non-positive size means "keep it".

**A window state change is a request to the platform.** Resizing, borderlessness, minimize, restore
and the screen-device change all report `CNA_RESULT_PLATFORM` when the platform refuses — which a
headless video driver does for a window it never really showed. A session with no native window at
all accepts them and does nothing, which is the canonical behavior. Both answers are correct, and a
caller that treats either as fatal will be wrong on some machine.

The window's protected hooks are **not** mapped, and that is the same test the component model
applied with the opposite result: this ABI supplies the derived class for a component, so a
component's protected hooks are callbacks; it does not derive the window, so the window's are out of
reach. The deciding question is never "is it protected" — it is "does this ABI have a derived class
to hang it on".

## The graphics device manager

The manager is the one runtime object a C caller **creates**: `cna_graphics_device_manager_create`
takes the game handle the canonical constructor takes a pointer to, and creating it registers the
manager as both the graphics device manager and the graphics device service. That is what
`cna_game_services_contains_ext` reports, and it closes the loop the component slice opened — the
service identities were named there before anything registered them. A game accepts exactly one; a
second creation is refused.

Preferences are recorded and then applied: every setter records a request and
`cna_graphics_device_manager_apply_changes` is what turns it into a device reset and a window change.
A platform that declines the reconfiguration reports `CNA_RESULT_PLATFORM`, the rule the window slice
established. The adapter inside a `CNA_GraphicsDeviceInformation` is named by **index**, not by
pointer, because a pointer into the runtime's adapter list is nothing a C caller could hold safely.

### Releasing a manager keeps the object alive

`cna_graphics_device_manager_destroy` disposes the manager, unregisters both services, invalidates
the handle — and **keeps the C++ object alive until the game is destroyed**.

That is not caution. The canonical game caches a raw `IGraphicsDeviceService*` the first time it
resolves one and never clears it: not when the service is unregistered, not when the manager is
disposed. Destroying the manager while its game still lives leaves the game dereferencing freed
memory on its very next frame, which AddressSanitizer reproduced as a heap-use-after-free the first
time this route was written the obvious way. A disposed manager still answers that cached pointer
correctly, because disposal does not touch the game-owned device it points at, so retaining the
object is both safe and invisible to a caller. The retained objects are freed with the game, which
is why the suite stays leak-clean.

### The device-settings event is an observation

In XNA, `PreparingDeviceSettings` is how an application overrides device settings before the device
is created. **In this runtime nobody can.** The canonical event-handler collection delivers its
argument as a `const` reference, so the argument type's mutable accessor is unreachable from any
subscriber — C++ as much as C. This ABI hands the handler a read-only configuration and says so,
rather than inventing a power the canonical event does not grant. Change the settings through the
manager's own preference routes and apply them.

`IGraphicsDeviceManager` is an interface a caller could in principle implement, and this ABI does
**not** let one: the runtime creates the manager itself and the game resolves it through the service
container, so there is no seam for a caller-provided implementation. That is the same question the
component model answered the other way, and the difference is who constructs the object.

## The game's content manager is borrowed

A game owns its content manager as a **value member**, so `cna_game_get_content_manager_ext` answers
a borrowed handle rather than an owned one: the same handle every time it is asked, refused by
`cna_content_manager_destroy`, and released when the game is. Every other content-manager route
accepts it, so a caller can set the root directory the game loads from and load through the game's
own cache. Destroying the game while holding the handle is allowed — the handle simply becomes
invalid — which is the opposite of an owned manager, and the difference is exactly who owns the
object.

`cna_game_set_content_manager_ext` **copies**, because the canonical setter takes a reference and
copy-assigns. The caller keeps its own manager, later changes to it never reach the game, and the
borrowed handle keeps addressing the game's own object rather than the source.

## The one runtime type C cannot bind

`CNA::Runtime` — the facade that would turn graphics, audio, input and content on and off before a
game exists — is **declared and defined nowhere**. All five of its methods would fail to link if
anything called them, nothing in the tree calls them, no translation unit includes `CNA/Misc.hpp`,
and the compiled runtime archive contains no `CNA::Runtime::` symbol at all. This ABI cannot bind a
symbol that does not exist, so its rows are recorded not-applicable rather than left planned as if
they were work waiting to be done.

`RuntimeOptions` is a sound four-flag value on its own, but its only purpose is to parameterize
`Initialize`. Mapping it alone would hand a C consumer a structure that configures nothing.

The `CApi_UnimplementedRuntimeFacade` test is what keeps that record honest: it inspects the built
runtime archive and fails the moment any `CNA::Runtime::` symbol appears. If the facade is ever
implemented, the check fires and the coverage rows become real work instead of quietly staying
wrong.

What a caller actually needs from this area already exists elsewhere and is answered honestly:
`cna_graphics_ext_is_available` and `cna_devices_ext_is_available` report which extension layers this
build contains, and the audio and renderer capability queries report what the machine can do.
