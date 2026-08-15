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
