# Emscripten main-loop `Game` object lifetime

## Summary

Under Emscripten, a `Microsoft::Xna::Framework::Game` (or subclass) instance driven through
`Game::Run()` **must** be heap-allocated (or otherwise given static/global storage duration).
It must never be a local variable in the calling function (typically `main()`), or in any
function whose stack frame is still active when `Run()` reaches its Emscripten main-loop setup.

Getting this wrong does not fail loudly at the point of the mistake. It fails many frames later,
deep inside an apparently unrelated virtual call — typically the first `Game::BeginDraw()` —
as a WebAssembly indirect-call fault:

- Debug builds: `RuntimeError: table index is out of bounds`
- Release builds: `null function or function signature mismatch`

There is a second symptom, reached earlier, whenever the browser delivers a window event before the
first draw. `Game`'s destructor takes CNA's platform (`Game::platform_`) with it, and
`~Sdl3Platform()` releases every subsystem that instance acquired — `SDL_INIT_VIDEO` included. The
window object outlives the subsystem that owns it, so the next platform query on it fails:

```text
SDL_GetWindowSize failed: Video subsystem has not been initialized
```

That message is worth recognising on sight. It reads like an initialisation-order or canvas-sizing
problem in a game that has just finished creating its window and GL context successfully, and it
has been misread as one more than once — once inside this repository (commit `bd2ddc4c0`, which
called it "a transient SDL3-on-Emscripten startup race" 2.5 hours before the real cause below was
proven), and once in a consuming project, where it was filed as "the canvas never gets sized".
It is not transient and not about sizing: it is what a destroyed `Game` looks like from the
main-loop callback that is still running.

## Root cause

`Game::RunLoop()`'s Emscripten path ends with:

```cpp
emscripten_set_main_loop(EmscriptenMainLoopCallback, 0, /*simulateInfiniteLoop=*/1);
```

`simulateInfiniteLoop=1` is implemented by the Emscripten runtime (`src/lib/libeventloop.js`,
function `$setMainLoop`) as a raw JavaScript `throw 'unwind'` once the callback is registered.
This lets code shaped like a traditional blocking game loop call it and have the call site "never
return", without truly blocking the browser's JS thread — control instead returns to the browser
event loop, which invokes the registered callback once per frame from then on.

CNA compiles with `-fwasm-exceptions` (the native WebAssembly exception-handling proposal — see
root `CMakeLists.txt`). Under that model, the `catch_all`/cleanup landing pad the compiler
generates for a local object with a non-trivial destructor is specified to catch **any**
exception unwinding through it, including a foreign, non-C++ value like this JavaScript
`'unwind'` string — not only genuine C++ exceptions. So the `throw 'unwind'` **does** trigger
real, synchronous C++ destructor execution for every local object with a non-trivial destructor
between the `emscripten_set_main_loop` call site and wherever the throw is ultimately caught
(back in Emscripten's own JS runtime, at the original `main()` invocation) — including a `Game`
subclass, if it happens to be a local variable in one of those frames.

This is proven, not inferred, in `spikes/emscripten-mainloop-stack-spike/repro.cpp`: a stack-local
object owning a resource via `std::unique_ptr` (mirroring how a real `Game` subclass owns its
`GraphicsDeviceManager`) has its destructor — and its owned resource's destructor — run
**immediately** after the `emscripten_set_main_loop` call, before a single main-loop tick
executes; the equivalent native build only destructs at the natural end of `main()`, as expected.
See that directory's `README.md` for the full write-up, including the alternative hypothesis
(`GameServiceContainer`/multiple-inheritance pointer adjustment) that was investigated and ruled
out with sanitizer-verified reproductions before this was found.

Mapped onto the real code: `GraphicsDeviceManager` is owned by a `std::unique_ptr` member of the
user's `Game` subclass (e.g. `SvgDomSmokeTest::gdm_`). `Game` itself only holds a raw, non-owning
`IGraphicsDeviceManager* graphicsDeviceManager_`, captured once via
`GameServiceContainer::GetService<IGraphicsDeviceManager>()` in `Game::DoInitialize()`. If the
`Game` subclass is a stack local, the spurious unwind-triggered destructor deletes the real
`GraphicsDeviceManager` through that `unique_ptr`, while `Game::graphicsDeviceManager_` keeps
pointing at the now-freed heap block. The dangling pointer doesn't fault immediately — only once
that freed memory is reused by something else (SDL event structures, sprite data, ...) and
`Game::BeginDraw()` dereferences it to make a virtual call does the corrupted vtable-adjacent
memory get misread as a WebAssembly function-table index, producing the observed crash deep
inside `BeginDraw()`, long after the object was actually destroyed.

## The fix

This is a call-site object-lifetime requirement, not a defect in `Game`, `GraphicsDeviceManager`,
or `GameServiceContainer` themselves (both were independently sanitizer-verified correct — see
the spike's `README.md`). The fix applied in this pass:

1. `Game::Run()`'s own Doxygen comment
   (`modules/runtime/include/Microsoft/Xna/Framework/Game.hpp`) documents this constraint in
   full, next to the API.
2. Every existing CNA example whose `main()` is genuinely reachable under an Emscripten build was
   audited and, where it stack-allocated its `Game` subclass, changed to heap-allocate it instead
   (`new`, deliberately never `delete`d — correct for an app object meant to live for the page's
   lifetime):
   - `modules/renderers/svg-dom/examples/{svgdom_smoke_test,svgdom_pixel_verification_test,svgdom_scissor_order_test}.cpp`
   - `modules/renderers/html-dom/examples/{htmldom_smoke_test,htmldom_pixel_verification_test,htmldom_stress_test,htmldom_dispose_test,htmldom_memory_test,htmldom_visual_demo}.cpp`
   - `modules/renderers/canvas/examples/{canvas_smoke_test,canvas_graphics_capability_test}.cpp`
   - `modules/graphics/examples/house3d_demo.cpp` (the general 3D demo, also Emscripten-buildable)

   `modules/graphics/examples/demo_2d/src/Main.cpp`, `modules/devices/examples/demo_devices/src/Main.cpp`,
   and `modules/input/examples/demo_input/src/Main.cpp` already heap-allocated their `Game`
   object and needed no change.
3. A permanent, minimal, always-reproducing spike (`spikes/emscripten-mainloop-stack-spike/`) keeps the
   failing pattern demonstrable for future maintainers without needing to rediscover it, per this
   repository's existence-gate-spike convention.

## What CNA does when it happens anyway

A consuming project can still get this wrong, so the framework does not depend on it being right.
Two call sites absorb the fallout rather than turning it into a crash — neither repairs a destroyed
`Game`, and neither is a substitute for the rule at the top of this file:

- `Sdl3Window::GetClientBounds()` keeps the last successfully queried bounds instead of throwing,
  so a bounds read never reports a nonsensical 0x0 window.
- `GraphicsDevice::UpdateViewportFromWindow()` guards its platform-window queries, so the
  `GameWindow.ClientSizeChanged` subscriber — which runs from inside the frame's event pump,
  because the browser delivered a resize — cannot unwind the game loop over one refused query.
  Deliberately narrow: `GraphicsDevice::createRenderer()` makes the same `GetPixelSize()` call and
  stays strict, because a renderer that cannot learn its surface size is a real failure the caller
  asked for. `GraphicsDevicePlatformWindowTests` covers both halves.

## What this is not

This is **not** a `GameServiceContainer` defect, and not specific to any one renderer. It
reproduced identically for `HTML_DOM` and `SVG_DOM` before the fix (both drive their examples
through the exact same `Game::Run()` under Emscripten), and would reproduce for any other
Emscripten-targeting `Game` subclass that stack-allocates itself in `main()`.
