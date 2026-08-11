// Minimal, self-contained, 100%-reproducing demonstration of the root cause behind the
// "table index is out of bounds" / "null function or function signature mismatch" crash that
// used to happen inside Microsoft::Xna::Framework::Game::BeginDraw() under Emscripten. See
// docs/emscripten-mainloop-game-lifetime.md for the full writeup and the real-code fix.
//
// Build and run:
//
//   emcc -std=c++23 -O0 -fwasm-exceptions -sEXIT_RUNTIME=1 -o repro.js repro.cpp && node repro.js
//
// Expected output shows "Owned DESTRUCTED" firing immediately after the call to
// emscripten_set_main_loop(..., 1) -- before a single main-loop tick has run -- and every
// subsequent tick then observing a null/reset resource.
//
// ---------------------------------------------------------------------------------------------
// Mechanism (proven, not guessed):
//
// emscripten_set_main_loop(fn, fps, /*simulateInfiniteLoop=*/1) is implemented (emsdk's
// src/lib/libeventloop.js, function $setMainLoop) as a raw JavaScript `throw 'unwind'` once the
// loop callback is registered, so that a caller written like a traditional blocking game loop can
// call it near the top of that loop and have the call site "never return" without truly blocking
// the browser thread.
//
// CNA compiles with -fwasm-exceptions (the native WebAssembly exception-handling proposal, see
// root CMakeLists.txt). Under that model, a `try`/cleanup landing pad generated for any local
// object with a non-trivial destructor in an unwound frame is specified to catch ANY exception
// propagating through it via `catch_all`, including a foreign, non-C++, JavaScript-thrown value
// like this 'unwind' string -- not only genuine C++ exceptions. This is standards-conformant
// behaviour for `catch_all`, and it is exactly what this repro demonstrates: the JS throw crosses
// main()'s wasm frame, main()'s cleanup landing pad for its local `GameLike game;` fires, and
// GameLike's (and its owned Owned's) destructors run for real -- immediately, synchronously,
// right there at the emscripten_set_main_loop call site, long before the "loop" the calling code
// thinks it just started has produced a single frame.
//
// The failure mode this produces in a real Game is subtle because the *Game object's own stack
// memory is untouched* (nothing overwrites it) -- what breaks is anything the destructor chain
// tears down that a *raw, non-owning pointer* elsewhere still references. In CNA's real
// Game/GraphicsDeviceManager pair: GraphicsDeviceManager is owned by a `std::unique_ptr` member
// of the user's Game subclass; Game itself only holds a raw, non-owning
// `IGraphicsDeviceManager* graphicsDeviceManager_` (obtained once via
// GameServiceContainer::GetService<IGraphicsDeviceManager>()). If the Game subclass is a local
// variable in main(), this premature unwind-triggered destruction deletes the real
// GraphicsDeviceManager through that unique_ptr while Game::graphicsDeviceManager_ keeps pointing
// at the now-freed heap block. The dangling pointer itself doesn't fault -- only many frames
// later, once that freed heap memory has been reused by something else (SDL event structures,
// sprite data, ...) and Game::BeginDraw() dereferences it to make a virtual call, does the
// corrupted vtable-adjacent bytes get read as a WebAssembly function-table index, producing the
// observed indirect-call fault deep inside completely unrelated-looking code.
//
// The fix is therefore call-site lifetime, not a Game/GraphicsDeviceManager/GameServiceContainer
// defect: a Game (or subclass) instance driven through Run() under Emscripten must never be a
// local variable in a frame that emscripten_set_main_loop's unwind will cross -- heap-allocate it
// (`new`, never deleted -- correct and intentional for a page-lifetime app object) or give it
// static/global storage. See Game::Run()'s own doc comment
// (modules/runtime/include/Microsoft/Xna/Framework/Game.hpp) for the same explanation kept next
// to the API, and docs/emscripten-mainloop-game-lifetime.md for the audit of every example this
// pass fixed.
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <cstdio>
#include <memory>

struct Owned
{
    int tag;
    explicit Owned(int t) : tag(t) { std::printf("Owned(%d) constructed\n", t); }
    ~Owned() { std::printf("Owned(%d) DESTRUCTED\n", tag); }
};

// Mirrors the real shape: GameLike (stands in for a Game subclass) owns its "device manager"
// through a unique_ptr, exactly like SvgDomSmokeTest::gdm_ / HtmlDomSmokeTest::gdm_ do.
struct GameLike
{
    std::unique_ptr<Owned> owned;
    int frame = 0;

    GameLike() : owned(std::make_unique<Owned>(42))
    {
        std::printf("GameLike constructed, owned=%p\n", static_cast<void*>(owned.get()));
    }

    ~GameLike() { std::printf("GameLike DESTRUCTED (owned is reset as part of this)\n"); }

    void run();
};

// Mirrors Game::graphicsDeviceManager_: a raw, non-owning pointer captured once, before the loop
// starts, exactly like Game::DoInitialize()'s Services_.GetService<IGraphicsDeviceManager>().
static Owned* g_nonOwningPtr = nullptr;
static int g_frame = 0;

#if defined(__EMSCRIPTEN__)
static void mainLoopCallback()
{
    ++g_frame;
    std::printf("tick %d: g_nonOwningPtr=%p\n", g_frame, static_cast<void*>(g_nonOwningPtr));
    // A real Game would dereference this pointer here (Game::BeginDraw() ->
    // graphicsDeviceManager_->BeginDraw()). Since the pointee was already destroyed above, this
    // would be a use-after-free in a real program; this repro only prints the pointer value
    // rather than actually dereferencing freed memory, to keep the demonstration itself well
    // defined.
    if (g_frame >= 3)
    {
        std::printf("=== 3 ticks observed; g_nonOwningPtr pointed at an already-destroyed object "
                     "the entire time ===\n");
        emscripten_cancel_main_loop();
    }
}
#endif

void GameLike::run()
{
    g_nonOwningPtr = owned.get();
#if defined(__EMSCRIPTEN__)
    std::printf("about to call emscripten_set_main_loop(..., simulateInfiniteLoop=1)\n");
    emscripten_set_main_loop(mainLoopCallback, 0, 1);
    std::printf("!!! unreachable if simulateInfiniteLoop worked as documented !!!\n");
#else
    for (int i = 0; i < 3; ++i)
    {
        std::printf("tick %d (native, no unwind trick): g_nonOwningPtr=%p, tag=%d\n", i + 1,
                    static_cast<void*>(g_nonOwningPtr), g_nonOwningPtr->tag);
    }
#endif
}

// Mirrors Game::Run() being reached via a couple of nested stack frames, like the real
// SvgDomSmokeTest::Run() -> Game::Run() -> Game::RunLoop() chain.
static void callRunNested(GameLike& g)
{
    g.run();
    std::printf("callRunNested: after g.run() returned (should not print for the Emscripten build)\n");
}

int main()
{
    std::printf("=== main() start ===\n");
    // Deliberately a STACK LOCAL, matching the bug this repro demonstrates. This is exactly the
    // pattern every CNA Emscripten example used before this remediation pass (see
    // docs/emscripten-mainloop-game-lifetime.md) -- do not "fix" this repro to heap-allocate; a
    // heap-allocated variant belongs in the real examples, not here, since the whole point of
    // this file is to keep the failing pattern reproducible for future maintainers.
    GameLike game;
    callRunNested(game);
    std::printf("main() reached its end.\n");
    return 0;
}
