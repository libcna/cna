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

## Implementation

The former implementation called
`emscripten_set_main_loop(..., simulateInfiniteLoop=1)`. Emscripten implemented that API shape by
unwinding the C++/Wasm caller, which destroyed a local `Game` while the registered callback still
held its address. The next frame then failed as a dangling-pointer virtual call.

CNA links application-style Emscripten executables with JS-lowered C++ exceptions and Asyncify.
Native WebAssembly
exception handling cannot be combined with Asyncify in the supported Emscripten toolchain.
`RunLoop()` invokes the frame body on its existing Wasm stack and awaits
`requestAnimationFrame()` through `EM_ASYNC_JS` only to suspend that same stack between frames. A
second C++/Wasm callback never re-enters a suspended Wasm instance, and the
caller is not unwound. This preserves XNA's blocking `Run()` lifetime contract while still yielding
the browser thread.

The exception ABI and blocking-loop facility are separate CMake interfaces:

```text
CNA::EmscriptenExceptionAbi:
-fexceptions
-sDISABLE_EXCEPTION_CATCHING=0

CNA::EmscriptenAsyncify:
-sASYNCIFY=1
```

`CNA::BuildConfig` propagates only the exception ABI. CNA-owned application executables opt into
Asyncify automatically; an external final executable that uses blocking `Game::Run()` links
`CNA::EmscriptenAsyncify` (the compatibility `CNA::EmscriptenAbi` composition contains both).

The generated `cna_c_api_wasm` library is intentionally different. JavaScript owns its event loop
and calls `cna_game_run_one_frame` from `requestAnimationFrame`, so its final link explicitly uses
`-sASYNCIFY=0`. This prevents Asyncify rewind from re-entering an exported i64-handle route without
the original JavaScript `BigInt` argument.

## Regression coverage

The web template and CNA renderer examples intentionally use ordinary local ownership. Runtime
browser checks must reach multiple `Update()`/`Draw()` frames, call `Exit()`, return from `Run()`,
and destroy the game normally. Merely compiling the page does not verify this contract.

The separate `CApi_WasmBrowserProbe` drives 5 frames externally, verifies exact Update/Draw counts,
BigInt handles, no page errors or unhandled rejections, and clean destruction. The qualified
WEBGL2 artifact also passed 60- and 600-frame canaries.

`emscripten-mainloop-stack-spike/` remains as the minimal reproduction of the obsolete unwind-based
implementation and documents why heap allocation previously appeared necessary.
