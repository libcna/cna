# CNA C API Callbacks, Threading and Shutdown

## Callback representation

Callbacks use an explicitly typed C function pointer and a caller-owned `void* context`. Game
lifecycle callbacks return `CNA_Result`; data arguments are pointers to const C POD structs; CNA
object handles supplied during a callback are borrowed. Callback declarations document whether the
context can be null and the exact ownership of every handle passed to them.

The owner keeps a callback context valid until the associated registration is successfully removed
or its source object/runtime has finished destruction. CNA does not take ownership of arbitrary
context memory. Callback registrations are represented by owned handles, so unregistration and
source destruction are observable and testable.

## Threading baseline

The first ABI has a conservative default: all runtime lifecycle, game, graphics, content, input,
media, device and resource-destruction operations must be called on the runtime's creation thread.
That thread is also the graphics thread for the initial game loop. APIs may later document a
stronger thread-safe guarantee only with dedicated tests; absence of such wording means the
creation-thread rule applies.

Audio callbacks and native worker activity may occur internally, but the public ABI does not expose
their synchronization primitives. An operation called on the wrong thread returns
`CNA_RESULT_THREAD` rather than touching thread-affine native state. Any later deferred-release or
main-thread-dispatch facility must state queue ownership, completion semantics and shutdown rules.

## Lifecycle callbacks and re-entrancy

Lifecycle callbacks execute synchronously on the creation thread at CNA's documented safe points.
Within an update/draw callback, a caller may use the explicitly callback-safe graphics/input
operations and request loop exit. It must not recursively enter the run/tick loop, destroy the
active runtime, unregister the currently executing callback, or invoke an operation documented as
non-reentrant. Such calls return `CNA_RESULT_INVALID_STATE` where CNA can diagnose them.

A callback result other than `CNA_RESULT_SUCCESS` is handled as described in
[`ERRORS.md`](ERRORS.md); C code must return normally. Throwing a C++ exception, `longjmp` across a
CNA frame or freeing a live callback context is unsupported behavior.

## Shutdown order

1. Request that the game loop exit and allow the current callback to return.
2. Unregister optional callbacks and release owned children in their documented parent order.
3. Release the game/runtime handle on its creation thread.
4. Do not call CNA with any remaining handle after successful shutdown.

Runtime destruction synchronizes callback teardown before it invalidates handles. It then releases
native resources, records disposal failures through the error contract, and marks the runtime
closed. A new runtime may be created only after the previous runtime has fully closed.
