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

CNA now links Emscripten consumers with JS-lowered C++ exceptions and Asyncify. Native WebAssembly
exception handling cannot be combined with Asyncify in the supported Emscripten toolchain.
`RunLoop()` invokes the frame body on its existing Wasm stack and awaits
`requestAnimationFrame()` through `EM_ASYNC_JS` only to suspend that same stack between frames. A
second C++/Wasm callback never re-enters a suspended Wasm instance, and the
caller is not unwound. This preserves XNA's blocking `Run()` lifetime contract while still yielding
the browser thread.

The flags are exported through CNA's `cna_build_config` interface so the framework, its dependencies,
and the final game executable use one exception ABI:

```text
-fexceptions
-sDISABLE_EXCEPTION_CATCHING=0
-sASYNCIFY=1
```

## Regression coverage

The web template and CNA renderer examples intentionally use ordinary local ownership. Runtime
browser checks must reach multiple `Update()`/`Draw()` frames, call `Exit()`, return from `Run()`,
and destroy the game normally. Merely compiling the page does not verify this contract.

`emscripten-mainloop-stack-spike/` remains as the minimal reproduction of the obsolete unwind-based
implementation and documents why heap allocation previously appeared necessary.
