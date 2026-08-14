# CNA C API Callbacks, Threading and Shutdown

## Callback representation

Callbacks use an explicitly typed C function pointer and a caller-owned `void* context`. The first
implemented callback table is `CNA_GameCallbacks`, copied by `cna_game_create`. Its function
pointers and context remain caller-owned and must remain valid until `cna_game_destroy` returns.
The game handle supplied to a lifecycle callback is borrowed for that callback duration; it may be
used with callback-safe operations but must not be retained or destroyed. A callback may derive a
borrowed graphics-device handle with `cna_game_get_graphics_device`; CNA generation-invalidates
that child handle before returning to the native game loop.

Each game lifecycle callback receives a CNA-initialized `CNA_CallbackError`. If the callback returns
anything other than `CNA_RESULT_SUCCESS`, it may fill the versioned `message` string view. CNA
copies valid UTF-8 diagnostic bytes before the callback returns, stops the game at a safe point and
reports `CNA_RESULT_CALLBACK` to the enclosing C API caller. An invalid, empty or omitted callback
diagnostic becomes a stable generic callback failure message.

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
The current game callbacks may call `cna_game_request_exit`, `cna_game_clear` and
`cna_game_set_window_title`; they must not recursively call `cna_game_run`,
`cna_game_run_one_frame` or `cna_game_destroy`. These non-reentrant calls return
`CNA_RESULT_INVALID_STATE`. Game timing is non-null only for update and draw; it is null for load,
unload and exit notifications.

A callback result other than `CNA_RESULT_SUCCESS` is handled as described in
[`ERRORS.md`](ERRORS.md); C code must return normally. Throwing a C++ exception, `longjmp` across a
CNA frame or freeing a live callback context is unsupported behavior.

## Shutdown order

1. Request that the game loop exit and allow the current callback to return.
2. Unregister optional callbacks and release owned children in their documented parent order.
3. Release the game/runtime handle on its creation thread.
4. Do not call CNA with any remaining handle after successful shutdown.

`cna_game_destroy` synchronizes callback teardown before it invalidates the game handle. It invokes
the exit then unload callbacks when they have not already occurred, releases native resources and
allows a new C-owned game to be created only after the previous handle has fully closed.
