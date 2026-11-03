# Emscripten `Game::Run()` lifetime

## Contract

Emscripten CNA follows the same ownership rule as native CNA and XNA: a `Game` subclass may be a
local object, a `std::unique_ptr`, or otherwise normally owned by its caller. `Game::Run()` keeps
that caller alive, runs until `Exit()` is requested, and then returns.

```cpp
int main()
{
    MyGame game;
    game.Run();
}
```

No page-lifetime leak or Emscripten-specific heap allocation is required.

This is the application-style C++ blocking-loop contract. The generated C API library instead
uses host-driven frames, described separately below.

## Implementation

The former implementation called
`emscripten_set_main_loop(..., simulateInfiniteLoop=1)`. Emscripten implemented that API shape by
unwinding the C++/Wasm caller, which destroyed a local `Game` while the registered callback still
held its address. The next frame then failed as a dangling-pointer virtual call.

CNA links application-style Emscripten executables with JS-lowered C++ exceptions and Asyncify.
The selected toolchain/build contract keeps native WebAssembly exception handling separate from
this Asyncify path.
`RunLoop()` invokes the frame body on its existing Wasm stack and awaits
`requestAnimationFrame()` through `EM_ASYNC_JS` only to suspend that same stack between frames. A
second C++/Wasm callback never re-enters a suspended Wasm instance, and the
caller is not unwound. This preserves XNA's blocking `Run()` lifetime contract while still yielding
the browser thread.

Browser scheduling is the only separate layer. Native `Tick()` calls the shared tick state machine
with fixed-step waiting enabled; `RunLoop()` and `RunFrameEXT()` call it without waiting because
`requestAnimationFrame()` or the embedding host already provides the next execution opportunity.
Both paths use the same `GameTime`, `TimeSpan` accumulator, 500 ms `MaxElapsedTime`, slow-frame
history, reset, draw suppression, exit, disposal and renderer-context logic. There is no static
Emscripten `Game*`, copied `GameTime` or floating-point millisecond accumulator to survive from one
game into another. A frame exception is logged by `Run()` and continues unwinding as it does
natively; the browser path does not turn it into a normal `EndRun()`.

The exception ABI and blocking-loop facility in `cmake/BuildPerformance.cmake` are separate
CMake interfaces:

```text
CNA::EmscriptenExceptionAbi:
-fexceptions
-sDISABLE_EXCEPTION_CATCHING=0

CNA::EmscriptenAsyncify:
-sASYNCIFY=1
```

`CNA::BuildConfig` propagates only the exception ABI. CNA-owned application executables opt into
Asyncify automatically; a source-build consumer's final executable that uses blocking
`Game::Run()` links `CNA::EmscriptenAsyncify` (the compatibility `CNA::EmscriptenAbi` composition
contains both).
In a build that compiles the `WEBGPU` renderer in, `CNA::EmscriptenAsyncify` also carries
`-sASYNCIFY_STACK_SIZE=65536`, because that renderer suspends deeper than Emscripten's default
4 KiB unwind buffer holds (see [webgpu-renderer.md](webgpu-renderer.md)); nothing has to be added
by hand.

The installed C++ SDK exports the same three targets. An installed consumer linking `CNA::CNA` and
`CNA::EmscriptenAbi` ran blocking `Game::Run()` through normal `Exit()`, return and destruction in
headless Chrome (WEBGPU, MTH-PLAT-004); see [installed-cpp-sdk.md](installed-cpp-sdk.md).
A pure-C program of the installed static C API opts in with `CNA::CApiStaticAsyncify`, which
carries `CNA::EmscriptenAsyncify`'s options, to call blocking `cna_game_run`
([c-api/CONSUMING.md](c-api/CONSUMING.md#emscripten)).

The generated `cna_c_api_wasm` library is intentionally different. JavaScript owns its event loop
and calls `cna_game_run_frame_ext` from `requestAnimationFrame`, so its final link explicitly uses
`-sASYNCIFY=0`. This prevents Asyncify rewind from re-entering an exported i64-handle route without
the original JavaScript `BigInt` argument.

Use the host-driven `cna_game_run_frame_ext` loop for that artifact. The C library's lack of
Asyncify must not be applied to a C++ executable that calls blocking `Game::Run()`, nor should
enabling Asyncify on a JavaScript-entered C artifact be treated as a lifetime repair.

The host frame path initializes and begins the run once, performs one non-waiting shared tick per
call, and returns false after normal exit/end hooks. A terminal frame exception marks the host run
ended and propagates through the ordinary error boundary; it does not synthesize normal EndRun.
The qualified C browser probe is WEBGL2-specific; C++ WEBGPU browser application evidence has its
own renderer/Asyncify boundary.

## Regression coverage

The web template and CNA renderer examples intentionally use ordinary local ownership. Runtime
browser checks must reach multiple `Update()`/`Draw()` frames, call `Exit()`, return from `Run()`,
and destroy the game normally. Merely compiling the page does not verify this contract.

The separate `CApi_WasmBrowserProbe` drives `RunFrameEXT()` from animation frames. It verifies
the initialize/load/begin/update/draw/exiting/end order, variable-step `ResetElapsedTime()`, one-draw
suppression, no final draw after `Exit()`, exact Update/Draw counts, BigInt handles and clean
destruction. A second fixed-step game verifies that a browser frame with no update still draws and
inherits no timing state from the first game. The qualified WEBGL2 artifact also passed 60- and
600-frame canaries before this stronger lifecycle coverage replaced the old `RunOneFrame()` probe.

[spikes/emscripten-mainloop-stack-spike/](../spikes/emscripten-mainloop-stack-spike/README.md)
contains the reproduction of the obsolete unwind-based implementation and explains the former
heap-allocation workaround. It is historical evidence, not the current compiler/loop recipe.

A passing host-driven C probe does not by itself prove a stack-local C++ Game survives blocking
Run; that requires an application-style browser run through normal Exit, return and destruction.
Native frame/lifetime tests and source-level local ownership complement, rather than replace,
that browser check.
