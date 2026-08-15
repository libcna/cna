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
