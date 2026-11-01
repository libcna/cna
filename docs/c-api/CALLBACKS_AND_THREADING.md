# CNA C API Callbacks, Threading and Shutdown

This page defines the C ABI's cross-cutting defaults. The declaration beside a particular route or
callback remains the source of truth for its arguments, execution point and lifetime when it states
a more specific rule.

## Callback representation and context lifetime

CNA callbacks use an explicitly typed C function pointer plus a caller-owned `void* context`. CNA
never frees arbitrary context memory. A route that invokes a supplied callback synchronously and
does not retain it needs its context only until that route returns. A retained callback needs its
function and context until its registration is successfully released, its callback table is
replaced, or the documented source lifetime ends. The relevant header states which case applies.

`CNA_GameCallbacks` is copied by `cna_game_create`; `CNA_GameFrameHooks` is copied by
`cna_game_set_frame_hooks_ext`. The caller may therefore discard the table structure after the
installing call, but every non-null function and its context must remain valid until the table is
removed or `cna_game_destroy` returns. Other callback tables and registration routes document their
own copy and retention rules.

Where a subscribe route returns an owned registration handle, that handle makes unregistration
explicit and testable. It must be released exactly once with the matching unsubscribe route. Some
registrations keep their source alive, some belong to process-wide events, and game/window
registrations are invalidated when their game dies; do not infer one source-lifetime rule for every
event family. A few canonical registries deliberately have no unregister operation, which their
headers call out.

Game lifecycle callbacks and frame hooks are the callback family with a failure channel. CNA
initializes a versioned `CNA_CallbackError` before each call. A callback returns
`CNA_RESULT_SUCCESS` to continue; any other result stops the game at a safe point and the enclosing
C route reports `CNA_RESULT_CALLBACK`. Before returning a failure, the callback may put valid UTF-8
diagnostic bytes in `message`; CNA copies them immediately after the callback returns. The bytes
must survive that return: use static storage or storage owned by the registration context, and
keep it valid through the return of the C route dispatching the callback. Callback-local automatic
storage is not valid backing storage for this view. An absent, empty or
invalid diagnostic becomes the stable generic callback-failure message. Event callbacks that
return `void` only observe their event and cannot stop the game this way.

Every C callback must return normally. Throwing a C++ exception or using `longjmp` across a CNA
frame, retaining callback-only pointers or borrowed handles, and freeing a context while CNA may
still invoke it are unsupported.

The process-wide logger sink is the retained callback without a registration handle in this
cross-cutting layer. `cna_logger_set_sink_ext` replaces it and `cna_logger_reset_sink_ext` (or a
null replacement) restores stderr. It runs synchronously on whichever thread emits the line, under
the logger's serialization; its message view is valid only for that call. It must not re-enter CNA
because a nested operation may log while the native logger is still delivering the outer line.
Keep its context alive until the replacement/reset call returns; that return also establishes that
the previous callback is no longer in flight.

## Owner-thread default

Every runtime handle belongs to the thread that created it. Unless a route explicitly documents a
stronger guarantee, lifecycle, game, graphics, content, input, media, device, resource and destroy
operations using that handle must run on its owner thread. For a C-owned game this is also the
graphics thread. A disallowed call returns `CNA_RESULT_THREAD` before touching the thread-affine
native object.

The explicit exceptions are:

- pure POD initialization, query and normalization helpers whose declarations say that they touch
  no runtime state may run on any thread;
- `cna_sound_effect_play` and `cna_sound_effect_play_with_settings` may use a live sound-effect
  handle from any thread; all other sound-effect routes retain the normal rule;
- an owner-thread game may opt into serving qualifying foreign-thread handle calls as described
  below.

Input capture is not a POD-only operation: keyboard, mouse, gamepad and touch capture uses the live
game on its owner thread. The returned snapshot is an independent value, and its documented local
query helpers may then inspect a copy on any thread.

On Apple platforms the game owner must also be the **process main thread** when its renderer needs
a window. SDL's Cocoa and UIKit video drivers initialize only there. A renderer that needs either
driver is refused on another thread with a diagnostic naming the main-thread requirement; a
renderer that can use SDL's offscreen driver, such as SOFTWARE, may run there without a window
(Apple M4 AM4-074). Process exit from another thread while the main-thread game is alive remains
supported (AM4-122).

## Opt-in foreign-thread service

`cna_game_set_foreign_thread_calls_ext(game, CNA_TRUE)` enables the service queue on the game
owner thread; it is off by default. A call from another thread is queued only when a handle boundary
would otherwise refuse it solely because that thread does not own the handle. The caller blocks,
the game thread executes the operation, and the caller receives both its result and its structured
error record.

The game thread services the queue at the start of update and draw, inside a callback scope. It may
also call `cna_game_run_foreign_thread_calls_ext` explicitly while a callback waits for a worker;
this prevents the common deadlock in which the worker waits for its queued call and the game thread
waits for the worker. A service pass returns immediately when nothing is queued and, after serving
one call, continues for at most 8 ms while more calls arrive.

This facility does not make every route thread-safe and does not move ownership:

- calls without a foreign-owned handle execute on their calling thread as usual;
- the two any-thread sound-effect play routes execute directly rather than entering this queue;
- `cna_game_run`, `cna_game_run_one_frame`, `cna_game_run_frame_ext`, `cna_game_tick`,
  `cna_game_destroy`, `cna_game_set_foreign_thread_calls_ext` and
  `cna_game_run_foreign_thread_calls_ext` must themselves be called on the game thread and are
  never queued;
- disabling the facility or destroying the game completes still-waiting calls with
  `CNA_RESULT_INVALID_STATE`.

A binding must therefore pump the queue before joining or synchronously waiting for a worker that
may be using game-owned handles.

## Game callback scope and re-entrancy

Game lifecycle callbacks and frame hooks run synchronously on the game thread. The same callback
scope covers C game-component handlers; activated and deactivated game-event handlers also enter
that scope. `CNA_GameTime` is non-null only for the ordinary update and draw callbacks. Load,
unload, exit and every frame hook receive null timing; `begin_draw` instead receives its dedicated
`out_should_draw` value.

The game handle supplied to a lifecycle callback is borrowed for that callback. It may be used by
callback-safe routes but must not be retained or destroyed. `cna_game_request_exit`,
`cna_game_clear` and `cna_game_set_window_title` are valid callback operations, and the explicit
foreign-thread pump is designed for use there. Recursive game driving is not: `cna_game_run`,
`cna_game_run_one_frame`, `cna_game_run_frame_ext`, `cna_game_tick` and `cna_game_destroy` return
`CNA_RESULT_INVALID_STATE` while a callback scope is active.

`cna_game_get_graphics_device` lends one borrowed device handle in either of two windows:

- during a callback scope, until its outermost callback returns;
- before the first run, step or tick, from the owned game handle, until the first callback returns.

Repeated queries in one window return the same handle. The borrowed device has no release route and
becomes invalid at the end of its window; once a run has begun, an out-of-callback query is refused.
Owned textures, SpriteBatch objects, content managers and other resources created through the loan
have independent handles and may survive the callback, but remain owner-thread objects and must be
released in their documented parent order. See
[`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md) for the complete device-loan
contract.

A failed lifecycle callback suppresses later ordinary frame callbacks. A blocking or host-driven
run still delivers its completion hooks (`exiting`, then `end_run`), and later destruction still
delivers `unload_content` if content was loaded. Completion and cleanup callbacks may themselves
fail, but the game retains the first callback diagnostic so a later failure or unrelated C call
does not replace the result reported by the enclosing run, a terminal host-frame call or destroy.
The detailed error mapping is in [`ERRORS.md`](ERRORS.md).

## Shutdown order

1. Request exit and let the current callback return.
2. Stop or pump any worker that can still issue calls, then disable foreign-thread service if it was
   enabled.
3. Release owned children in their documented leaf-to-parent order and release ordinary callback
   registrations when their event-family contract requires it.
4. Call `cna_game_destroy` on the game owner thread.
5. Release any game/window registration handles deliberately retained across destruction, then do
   not call CNA with any remaining stale handle.

`cna_game_destroy` refuses with `CNA_RESULT_INVALID_STATE` while a game-owned texture, SpriteBatch,
content manager, audio object, game component or another tracked owned child remains live; the game
and child are unchanged by that refusal. With no blocking child it disables foreign-thread service,
delivers `exiting` once if it has not already occurred, disposes native state, delivers
`unload_content` when content was loaded, invalidates the game handle and allows a later C-owned
game to be created. A shutdown callback failure returns `CNA_RESULT_CALLBACK`, but the destroy
still releases the game and invalidates its handle.

Game and window event registrations are the deliberate child-lifetime exception. They do not block
`cna_game_destroy`, because a subscriber must be able to observe the game's own disposal.
Destruction raises the disposal event and then invalidates every live game/window subscription. Its
registration handle remains caller-owned: a later `cna_game_unsubscribe` succeeds without detaching
from the dead source, while a second release returns `CNA_RESULT_INVALID_HANDLE`.
`CApi_StressSmoke` exercises a game registration and a window registration in exactly that order.
