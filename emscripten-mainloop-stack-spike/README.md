# `emscripten-mainloop-stack-spike` — proven root cause of the `Game::BeginDraw()` Emscripten crash

Everything here has actually been run and has actually worked (or actually failed, on purpose) on
this machine. It exists because a prior remediation pass on the SVG_DOM renderer hit a crash
inside `Game::BeginDraw()` under Emscripten — reproducing identically for HTML_DOM — that blocked
real-browser verification, and initially suspected `GameServiceContainer`/multiple-inheritance
pointer adjustment. That hypothesis was investigated and **ruled out** (see below) before the real
root cause was found and proven here.

## The bug

Symptom: the first `Draw()` of a CNA `Game` running under Emscripten crashes inside
`Game::BeginDraw()` → `graphicsDeviceManager_->BeginDraw()`, with:

- Debug builds: `RuntimeError: table index is out of bounds`
- Release builds: `null function or function signature mismatch`

Both are WebAssembly indirect-call faults — the code tried to make a virtual call through a
pointer whose target no longer holds a valid vtable.

## Root cause

`Microsoft::Xna::Framework::Game::RunLoop()`'s Emscripten path calls
`emscripten_set_main_loop(EmscriptenMainLoopCallback, 0, /*simulateInfiniteLoop=*/1)`.
`simulateInfiniteLoop=1` is implemented (emsdk's `src/lib/libeventloop.js`, function
`$setMainLoop`) as a raw JavaScript `throw 'unwind'` once the callback is registered, so that
code shaped like a traditional blocking game loop can call it and have that call site "never
return" without actually blocking the browser's JS thread.

CNA compiles with `-fwasm-exceptions` (the native WebAssembly exception-handling proposal; see
root `CMakeLists.txt`). Under that model, a `catch_all`/cleanup landing pad generated for a local
object with a non-trivial destructor is specified to catch **any** exception propagating through
it — including a foreign, non-C++, JavaScript-thrown value like this `'unwind'` string, not only
genuine C++ exceptions. `repro.cpp` in this directory proves exactly that: it declares a
`GameLike` object (owning an `Owned` resource via `std::unique_ptr`, mirroring how a real
`Game` subclass owns its `GraphicsDeviceManager`) as a **stack local in `main()`**, then calls
`.run()`, which nests one frame deeper and calls `emscripten_set_main_loop(..., 1)`.

Running it shows `GameLike DESTRUCTED` / `Owned(42) DESTRUCTED` printed **immediately** — before a
single main-loop tick has executed — while the native (non-Emscripten) build only destructs at
the natural end of `main()`, after all three ticks:

```text
$ node repro.js
=== main() start ===
Owned(42) constructed
GameLike constructed, owned=0x14e68
about to call emscripten_set_main_loop(..., simulateInfiniteLoop=1)
GameLike DESTRUCTED (owned is reset as part of this)
Owned(42) DESTRUCTED
tick 1: g_nonOwningPtr=0x14e68
tick 2: g_nonOwningPtr=0x14e68
tick 3: g_nonOwningPtr=0x14e68
=== 3 ticks observed; g_nonOwningPtr pointed at an already-destroyed object the entire time ===

$ ./repro_native
=== main() start ===
Owned(42) constructed
GameLike constructed, owned=0x502000000010
tick 1 (native, no unwind trick): g_nonOwningPtr=0x502000000010, tag=42
tick 2 (native, no unwind trick): g_nonOwningPtr=0x502000000010, tag=42
tick 3 (native, no unwind trick): g_nonOwningPtr=0x502000000010, tag=42
callRunNested: after g.run() returned (should not print for the Emscripten build)
main() reached its end.
GameLike DESTRUCTED (owned is reset as part of this)
Owned(42) DESTRUCTED
```

Mapped onto the real code: `GraphicsDeviceManager` is owned by a `std::unique_ptr` member of the
user's `Game` subclass (e.g. `SvgDomSmokeTest::gdm_`). `Game` itself only holds a raw, non-owning
`IGraphicsDeviceManager* graphicsDeviceManager_`, obtained once via
`GameServiceContainer::GetService<IGraphicsDeviceManager>()`
(`Game::DoInitialize()`). If the `Game` subclass is a stack-local in `main()`, the spurious unwind
destructs it — deleting the real `GraphicsDeviceManager` through that `unique_ptr` — while
`Game::graphicsDeviceManager_` keeps pointing at the now-freed heap block. The dangling pointer
itself doesn't fault; only frames later, once that freed memory has been reused by something else
(SDL event structures, sprite data, ...) and `Game::BeginDraw()` dereferences it to make a virtual
call, does the corrupted vtable-adjacent memory get read as a WebAssembly function-table index —
producing the observed indirect-call fault deep inside completely unrelated-looking code, long
after the object was actually destroyed.

**The fix is call-site object lifetime, not a `Game`/`GraphicsDeviceManager`/`GameServiceContainer`
defect.** See `docs/emscripten-mainloop-game-lifetime.md` for the audit and fix applied to every
CNA example, and `Game::Run()`'s own doc comment
(`modules/runtime/include/Microsoft/Xna/Framework/Game.hpp`) for the same explanation kept next to
the API.

## Hypothesis ruled out first: `GameServiceContainer` / multiple-inheritance pointer adjustment

The initial, plausible-looking hypothesis was that `GameServiceContainer::AddService`/`GetService`
mishandles the non-primary-base pointer adjustment needed for
`GraphicsDeviceManager : Object, IGraphicsDeviceService, IDisposable, IGraphicsDeviceManager`
(`IGraphicsDeviceManager` is the 4th, non-primary base). Two targeted reproductions were built and
run under **native ASan+UBSan+vptr** and **Emscripten (`-sASSERTIONS=2 -sSAFE_HEAP=1`)**:

1. A hostile multiple-inheritance shape (`PaddingBaseA/B/C` + `IService`, matching
   `GraphicsDeviceManager`'s 4-base, non-primary-target shape) registered and retrieved through
   `GameServiceContainer`'s real `AddService<T>`/`GetService<T>` logic, then called through the
   retrieved pointer.
2. The same object registered under **two** interface types simultaneously (mirroring
   `GraphicsDeviceManager::registerServices()`'s real
   `AddService<IGraphicsDeviceManager>(this); AddService<IGraphicsDeviceService>(this);` pair),
   checking both retrieved pointers, both `type_index` hashes, and both virtual dispatches.

Both passed cleanly, with correct pointer adjustment, correct dispatch, and no sanitizer reports,
on every configuration tested. `GameServiceContainer`'s pointer-adjustment logic is correct: the
call-site implicit conversion from the concrete type to the explicitly-specified `TService*`
happens at compile time (a standard, ABI-defined static offset, not something `AddService`/
`GetService` compute themselves), and `static_cast<void*>`/`static_cast<TService*>` round-trip
that already-adjusted address exactly, with no virtual inheritance anywhere in the hierarchy to
complicate it. This is why the actual fix in this pass does not touch `GameServiceContainer` at
all — see `modules/runtime/tests/Microsoft/Xna/Framework/GameServiceContainerTests.cpp` for the
permanent regression coverage this investigation added anyway (a real gap: no prior test
registered a service under a non-primary base).

## Files

- `repro.cpp` — the decisive repro. Build both ways:
  ```bash
  # Native (no unwind trick — for comparison)
  g++ -std=c++23 -O0 -g -fsanitize=address,undefined -o repro_native repro.cpp && ./repro_native

  # Emscripten (reproduces the premature-destruction bug)
  emcc -std=c++23 -O0 -fwasm-exceptions -sASSERTIONS=2 -sEXIT_RUNTIME=1 -o repro.js repro.cpp
  node repro.js
  ```
  Built binaries (`repro_native`, `repro.js`, `repro.wasm`) are gitignored; only the source and
  this README are checked in, per this repository's existence-gate-spike convention.
