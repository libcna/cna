# Language bindings

How CNA is used from languages other than C++, and what a binding has to get right. This page is
the design and the reasoning; the exact contract a binding programs against is the C API
documentation in [`c-api/`](c-api/README.md), and where the two differ, `c-api/` is right.

CNA has **one** engine, written in C++. Every other language reaches it through CNA's C ABI:

```text
application in language X
        ↓
language-native CNA API         (the binding's public surface)
        ↓
language interop layer          (P/Invoke, FFI, JNI, WebAssembly imports, …)
        ↓
CNA C ABI                       (modules/c-api, headers CNA/C/*.h)
        ↓
CNA C++                         (modules/*)
        ↓
Sharp Runtime, CNA subsystems, renderers, native platform
```

A binding is a presentation layer over that ABI, never a second implementation of CNA.

## Where things stand

- **The C ABI** lives in this repository (`modules/c-api`). It is experimental (`0.x`); its current
  version and change history are in [`c-api/ABI_VERSIONING.md`](c-api/ABI_VERSIONING.md), and what
  it covers is in [`c-api/COVERAGE.md`](c-api/COVERAGE.md). A C program can use it directly;
  [`c-api/CONSUMING.md`](c-api/CONSUMING.md) and `modules/c-api/examples/c/hello_cna.c` show how.
- **C# / .NET — CNA.NET** ([`libcna/cna-dotnet`](https://github.com/libcna/cna-dotnet)) is the
  actively maintained language binding. It is not release-ready yet; its README states what has
  been measured. It admits exactly the ABI minor it was reviewed against and pins one CNA commit;
  [`handoff.md`](../handoff.md) (*Bindings*) states which one, and what it still has to adopt.
- **Go, Java, Python, Ruby, Rust, Swift and TypeScript** bindings, each with a template project,
  were built in 2026-08/09, requalified one last time on 2026-09-30 and archived into `cna-lab`.
  They stay pinned to old ABI versions and are not maintained. A Common Lisp binding
  (`cna-common-lisp`) existed too but was not part of that final pass, and where it lives now is
  not recorded here. The CNA defects all of them measured are in
  [`../plans/plan_bindings_upstream.md`](../plans/plan_bindings_upstream.md).

What a binding supports is always an intersection: what the binding implements, what the CNA
subsystem supports, what the selected renderer and platform support, and what the binding's
packaging delivers for that platform. A binding does not make every renderer or platform available.

### CNA.NET

CNA.NET has three layers, so that XNA's 2010-era names never constrain what CNA itself can offer:

| Project | Namespace | Role |
|---|---|---|
| `CNA.XnaCompat` | `Microsoft.Xna.Framework.*` | The XNA 4.0 public facade an existing game compiles against. |
| `CNA.Framework` | `CNA.*` | The idiomatic managed CNA API: local value types, resources that own their native handle, the game-loop callback adapter. Free to grow beyond XNA. |
| `CNA.Interop` | internal | `LibraryImport` declarations and ABI-shaped structs. |

C# programs use the real .NET runtime and base class library. Nothing in CNA.NET wraps Sharp
Runtime (see [below](#sharp-runtime-stays-on-the-native-side)).

## Why bindings go through one C ABI

**One engine.** Every binding shares the same graphics, renderers, audio, content, input and
platform code, so a fix in C++ reaches every language at once. Porting CNA to each language would
mean maintaining a parallel engine for each.

**The ABI is part of CNA, not a separate project.** A change to a C++ type and the change to its
C mapping land in the same commit. A separate `cna-c` repository would only add release lag and
version mismatches between the two.

**Not the C++ ABI, and not C++/CLI.** `std::string`, `std::vector`, templates, inheritance,
virtual dispatch, exceptions, RTTI, name mangling and standard-library layouts differ between
compilers and cannot be called safely from another language. C++/CLI would tie .NET to Windows
and help no other language. A C ABI made of fixed-width values, plain structs, opaque handles,
pointer-plus-length buffers, explicit UTF-8, result codes and C function pointers can be called
from almost anything.

**The ABI describes CNA, not a general runtime.** Its routes are domain routes
(`cna_game_*`, `cna_texture2d_*`, `cna_sprite_batch_*`, `cna_content_*`, …). It does not export
strings, lists or tasks as general-purpose objects.

## The boundary rules

Every binding relies on the same rules. Each one is specified, and tested, in `c-api/`:

| Rule | In short | Contract |
|---|---|---|
| Handles | Every native object is a `CNA_Handle` (`uint64_t`): a slot index plus a generation. A stale, double-released, wrong-kind or foreign handle is refused, never dereferenced. | [`HANDLES.md`](c-api/HANDLES.md) |
| Ownership | Every handle and pointer parameter is *owned*, *borrowed*, *retained*, *transferred*, a *callback context* or a *caller buffer*. Resources are released before their parent and the game goes last; event registrations are the exception and may outlive their source. | [`OWNERSHIP.md`](c-api/OWNERSHIP.md) |
| Errors | Every fallible route returns `CNA_Result`; details are queried per thread with `cna_error_*`. No C++, Sharp Runtime or platform exception ever crosses the boundary. | [`ERRORS.md`](c-api/ERRORS.md) |
| Text and buffers | Strings are UTF-8 `CNA_StringView`s going in and *query the size, then copy* coming out. Bulk data moves as pointer plus `uint64_t` length. Collections are count-then-copy, never a native container. | [`STRINGS_AND_BUFFERS.md`](c-api/STRINGS_AND_BUFFERS.md) |
| Types | Fixed-width integers, IEEE `float`/`double`, `CNA_Bool` (`uint8_t`, 0 or 1 only), `uint32_t` identifiers instead of C enums. No `size_t`, `long`, `wchar_t` or `bool`. | [`ABI_VERSIONING.md`](c-api/ABI_VERSIONING.md) |
| Extensible structs | Begin with `struct_size` and `struct_version`; fields are only ever appended. | [`ABI_VERSIONING.md`](c-api/ABI_VERSIONING.md) |
| Callbacks and threads | A C function pointer plus a caller-owned `void*` context. By default everything runs on the thread that created the runtime, and a call from another thread returns `CNA_RESULT_THREAD`. | [`CALLBACKS_AND_THREADING.md`](c-api/CALLBACKS_AND_THREADING.md) |
| Versions | `cna_get_abi_version()` first. Reject a different major; in `0.x` any minor may break, so admit only minors you have reviewed. | [`ABI_VERSIONING.md`](c-api/ABI_VERSIONING.md) |
| Capabilities | Ask the renderer and platform what they support instead of assuming. An unsupported operation returns `CNA_RESULT_NOT_SUPPORTED`; nothing is silently substituted. | [`RENDERERS_AND_CAPABILITIES.md`](c-api/RENDERERS_AND_CAPABILITIES.md) |
| Sharp Runtime | Invisible at the boundary. | [`SHARP_RUNTIME_BOUNDARY.md`](c-api/SHARP_RUNTIME_BOUNDARY.md) |

The ABI checks every value it can check — handles, lengths against limits, null pointers, UTF-8,
identifier ranges, struct prefixes, the calling thread — and answers such a mistake with a result
code. It cannot check what C cannot express: a non-null pointer to freed or too-small memory is
undefined behaviour, as in any C library. Nor can a result code stand in for a crash: a segmentation
fault, stack corruption, an assertion abort or a GPU driver crash still ends the process, which is
why sanitizer runs stay part of the testing. One CNA runtime may be active per process. The browser
build exports the same routes from a WebAssembly module:
[`c-api/WASM_ARTIFACT.md`](c-api/WASM_ARTIFACT.md).

## Sharp Runtime stays on the native side

Sharp Runtime is a C++ library that implements .NET-like APIs (`System::String`, collections,
streams, tasks, exceptions, …) for native code. **It is not the .NET runtime, it does not execute
C# assemblies, and a binding never sees it.** CNA uses it internally where .NET-like behaviour
helps the C++ implementation; the C ABI converts all of it, once, into neutral forms:

| Inside CNA | At the C ABI |
|---|---|
| `System::String` | UTF-8 view in, size/copy out |
| `List<T>`, spans | pointer plus count, or count/copy |
| dictionaries | explicit lookup and enumeration routes |
| `TimeSpan`, dates | fixed tick or duration values (`CNA_GameTime`) |
| `Stream` | a CNA stream handle or callback table |
| `Task<T>` | a synchronous route, or a completion callback plus polling |
| delegates, events | function pointer, context and registration handle |
| exceptions | `CNA_Result` plus error information |

So a binding:

- binds the C ABI only: it includes no Sharp Runtime header, calls no Sharp Runtime symbol and
  assumes no Sharp Runtime layout or ownership rule;
- uses its own language's strings, collections, paths, JSON, networking and async model
  (C# keeps `System.String`, `List<T>`, `HttpClient`, `Task<T>`);
- depends on a CNA ABI version, never on a Sharp Runtime version. A Sharp Runtime refactor that
  does not change CNA's behaviour changes no binding.

**Why wrapping Sharp Runtime would be a mistake.** It would give every language a second copy of
concepts it already has, add allocations and marshalling, couple binding releases to Sharp
Runtime's versions, and turn "bind CNA" into "bind CNA and all of Sharp Runtime". The boundary is
what keeps the binding effort scoped to CNA.

For a README, one sentence is enough: *CNA is implemented in C++ and uses Sharp Runtime internally;
bindings talk to CNA through its C ABI, and C# programs use the real .NET libraries.*

## Designing a binding

### Preserve concepts, adapt syntax

Every binding keeps XNA's mental model — `Game`, `GraphicsDevice`, `Texture2D`, `SpriteBatch`,
`ContentManager`, `Keyboard`, `Effect`, `Model` — but only C# copies XNA's literal API. Elsewhere
the language's own idioms win:

| Style | Languages | Direction | `Texture2D` width |
|---|---|---|---|
| XNA-compatible | C# | XNA 4.0 names, overloads and inheritance as closely as practical | `texture.Width` |
| Idiomatic object-oriented | Java, Kotlin, Swift, Python, Ruby, TypeScript | the language's properties, exceptions and lifecycle conventions | `getWidth()`, `texture.width` |
| Ownership-oriented | Rust, Zig, D, Nim | private handles, RAII / `Drop` / explicit `deinit` | `texture.width()` |
| Interface or callback-oriented | Go, C, Lua, functional languages | lifecycle as interfaces, records or callback tables rather than simulated inheritance | `texture.Width()` |

Rust should not pretend to be C#, Python should not expose C-style getters where a property is
natural, and Go should not simulate inheritance.

### Value types live in the language

Math and other small value types are implemented directly in the target language: `Vector2/3/4`,
`Matrix`, `Quaternion`, `Color`, `Rectangle`, `Point`, `BoundingBox`, `BoundingSphere`, `Plane`,
`Ray`, `MathHelper`, enums and immutable snapshots such as `GameTime`. Calling into native code for
`Vector2.Length()` would cost far more than the arithmetic. These values cross the ABI only as
arguments to native operations, as fixed-layout POD structs: `CNA_Vector2`, `CNA_Color` and
`CNA_Rectangle` in `core.h`, `CNA_GameTime` in `runtime.h`, and the 3D values listed in
[`c-api/MATH_AND_3D_VALUES.md`](c-api/MATH_AND_3D_VALUES.md).

Native-backed are the resources: `GraphicsDevice`, `Texture2D`, `RenderTarget2D`, buffers,
effects, `SpriteBatch`, `SoundEffect`, models and window/platform objects. A binding object for one
of them owns a `CNA_Handle`, borrows one, or fetches it again when it needs it. In CNA.NET a
`GraphicsDevice` asks for the callback-scoped device handle anew in each operation instead of keeping
it, and a `Model` is a managed aggregate of bones and meshes over handle-backed parts.

**Equality follows the same split.** Value types implement XNA's equality and hashing locally
(`Equals`, `GetHashCode`, `==`), field by field. A resource has identity instead, and the ABI does
not promise that two queries for the same native object return the same handle value: some routes
hand out a new owned alias each time ([`c-api/OWNERSHIP.md`](c-api/OWNERSHIP.md)). Where XNA
returns the same instance twice and a game relies on reference equality, the binding has to cache
its wrappers; comparing handle bit patterns is not enough.

### The game loop is native; the game is callbacks

No language can derive from a C++ class across an FFI. The native side owns the window, event
pumping, timing, frame lifecycle, graphics initialization and renderer selection, and calls the
binding through callback tables. `CNA_GameCallbacks`, copied by `cna_game_create`, carries
`load_content`, `update`, `draw`, `unload_content` and `exiting`; `CNA_GameFrameHooks`, installed
with `cna_game_set_frame_hooks_ext`, adds `initialize`, `begin_run`, `end_run`, `begin_draw` and
`end_draw`. A C# `Game` subclass, a Rust trait, a Go interface and a Python subclass all reduce to
those tables; in .NET the entry points are static `[UnmanagedCallersOnly]` methods.

Keep the crossings coarse: a handful of callbacks per frame, with each callback making the native
calls it needs. Control should not ping-pong between the two sides thousands of times a frame.

### Resource lifetime

Garbage-collected runtimes and native ownership meet only through handles, and the two lifetimes
must never be assumed equal.

- **Explicit release is the primary mechanism**: `IDisposable` in C#, `AutoCloseable` in Java,
  `Drop` in Rust, `with`/`close()` in Python, `dispose()` in JavaScript. Finalizers and GC cleanup
  are only a safety net — GPU and audio memory must not wait for a collector.
- In .NET, do not reach for `SafeHandle`: it is pointer-sized and cannot hold the 64-bit
  `CNA_Handle` on WebAssembly or a 32-bit target. CNA.NET's `NativeResourceHandle` keeps the same
  contract — release at most once, from `Dispose` or a critical finalizer — over the real 64-bit
  value.
- Native destruction happens on the creation thread. A finalizer thread or an arbitrary async
  continuation must not destroy a GPU resource; queue the release for the owning thread instead.
- Release child resources before their parent and every resource before the game; the ABI refuses
  the wrong order with `CNA_RESULT_INVALID_STATE` instead of crashing. Event registrations are the
  exception: they do not block destroying their source, which detaches them, and each registration
  handle is still released exactly once, in any order
  ([`c-api/CALLBACKS_AND_THREADING.md`](c-api/CALLBACKS_AND_THREADING.md)).
- Allocation and destruction always go through CNA routes, so no allocator or C runtime is ever
  shared across the boundary.

A binding's test suite has to cover normal, forgotten and double disposal, GC interaction,
release after the graphics device is gone, release on the wrong thread and shutdown order.
Lifetime is the most dangerous part of any binding.

### Callbacks and managed runtimes

While native code holds a callback pointer, the binding keeps its context alive: a `GCHandle` in
.NET, a global reference in JNI, a strong reference in Python, a boxed context in Rust, a callback
table entry in JavaScript. Unregister before freeing the context.

Callbacks arrive on CNA's creation thread, and each runtime has its own rule for entering managed
code there: the JVM needs the thread attached, Python needs the GIL, the browser has its main
thread and Web Workers, Rust has `Send`/`Sync`. Re-entrancy is limited as well: a lifecycle
callback may draw or request exit, but may not run or destroy the game. The rules are in
[`c-api/CALLBACKS_AND_THREADING.md`](c-api/CALLBACKS_AND_THREADING.md).

### Batch, snapshot and resolve once

The FFI boundary costs something on every crossing, so:

- **Batch high-frequency commands.** The public `SpriteBatch.Draw` stays idiomatic; the binding
  may collect draws and hand them over in bulk (`cna_sprite_batch_submit_many` takes an array of
  `CNA_SpriteCommand`). The same applies to vertex, index, texture and audio data: move whole
  buffers, never element by element.
- **Zero-copy only where a route says so.** Unless a route documents otherwise, CNA reads an input
  buffer only during the call and copies what it keeps
  ([`c-api/OWNERSHIP.md`](c-api/OWNERSHIP.md)). A binding that passes managed memory pins it for
  the duration of the call (`fixed` or a pinned `GCHandle` in .NET, critical arrays in JNI, the
  buffer protocol in Python) and never keeps a JavaScript view of WebAssembly memory across a call
  that may grow it. Where ownership, lifetime, mutability, alignment or layout is not stated, copy.
- **Take input as snapshots.** `Keyboard.GetState()` makes one native call and copies a
  `CNA_KeyboardState`; every `IsKeyDown` afterwards is local. Mouse, gamepad and touch work the
  same way ([`c-api/INPUT_SNAPSHOTS.md`](c-api/INPUT_SNAPSHOTS.md)). This is also exactly XNA's
  model.
- **Resolve names once.** Look `effect.Parameters["World"]` up natively once
  (`cna_effect_parameter_collection_find_name`) and cache the resulting handle; every later
  `SetValue` then uses the handle, not the string.
- **Measure before optimizing.** Correct, clear contracts come first; optimize the bottlenecks you
  have proven.

### Generic APIs dispatch in the language

C has no generics. `Content.Load<T>("player")` stays generic in C#: the binding dispatches on
`typeof(T)` to the typed route (`Texture2D`, `SoundEffect`, `Model`, …) and throws for a type it
does not know. The ABI's limitation does not have to appear in the public API.

### Errors

The binding turns `CNA_Result` plus the per-thread error information into the language's own
error model:

| Language | Mapping |
|---|---|
| C# | exceptions (`CnaException`, or the XNA exception the facade promises, e.g. `ContentLoadException`) |
| Java, Python, Ruby, Swift | that language's exceptions or errors |
| Rust | `Result<T, CnaError>` |
| Go | `error` |
| Zig | error unions |
| JavaScript / TypeScript | thrown `Error`, or a rejected `Promise` for asynchronous work |

`CNA_RESULT_NOT_SUPPORTED` deserves a precise mapping (`PlatformNotSupportedException`,
`UnsupportedOperationException`, `CnaError::NotSupported`): it means *this build or renderer cannot
do that*, which is different from a bug.

Asynchronous operations follow whatever completion contract the ABI defines for them. Today that
is either a single synchronous route (storage's XNA-style begin/end pairs,
[`c-api/STORAGE.md`](c-api/STORAGE.md)) or a completion callback plus a pollable pending state (the
Guide's keyboard input and message box, [`c-api/GAMER_SERVICES.md`](c-api/GAMER_SERVICES.md)); the
ABI has no general operation handle. A binding maps a genuinely deferred operation to `Task<T>`,
`Promise<T>`, a `Future` or `CompletableFuture`. It must not invent asynchronous variants of
synchronous routes, or two bindings will behave differently for the same operation.

### Renderer choice stays native

A binding may offer a renderer enum and capability queries, but never the renderer's internals
(`VkDevice`, `ID3D12Device`, `SDL_GPUDevice`, …). The ABI reports the active renderer and what it
supports. In a multi-renderer build the `cna_graphics_renderer_*_ext` family also lists the
renderers compiled in, accepts a preferred renderer and a fallback chain before the first device
exists, and reports which renderer was created and why others were passed over
([`c-api/RENDERERS_AND_CAPABILITIES.md`](c-api/RENDERERS_AND_CAPABILITIES.md)). A binding that
enumerates instead of hard-coding needs no new code when CNA gains a renderer. While the ABI is
`0.x`, though, a new renderer identity arrives in a new minor version, which the binding still has to
review and admit. A native-handle escape hatch, if one is ever wanted, is a separate, explicitly
labelled extension API.

### What to generate and what to design

Generation is right for raw FFI declarations, enum and constant tables, plain value structs,
repetitive resource-wrapper boilerplate, documentation tables and ABI parity checks. CNA itself
generates its C route boundaries from `modules/c-api/spec`
([`c-api/GENERATED_ROUTES.md`](c-api/GENERATED_ROUTES.md)).

People design the parts where a generated wrapper would compile and still be unsafe or unpleasant
to use: the game lifecycle, ownership, callbacks, generic content loading, the effect API,
collections, threading, error mapping, bulk transfer and the language's ergonomics. Never
translate every C++ header mechanically into every language.

## Packaging and versions

A user of a binding should not have to clone CNA, run CMake or hunt for a shared library. The
target experience is the language's normal package manager — `dotnet add package`,
`npm install`, `cargo add` — with the native CNA library for each supported platform inside the
package (`runtimes/<rid>/native/` for NuGet, the `.wasm` module for npm). Sharp Runtime is linked
into the native CNA library the package ships; nobody installs it separately.

Binding versions are their own: CNA, the C ABI and each binding release independently. A binding
declares the ABI versions it admits and checks the version at load time, failing with a clear
message on a mismatch.

## Testing a binding

Three layers of tests:

1. **CNA's own C++ tests** validate the engine.
2. **Pure C tests** (in this repository) prove the headers are real C and the ABI behaves:
   every public header compiles alone as C99 and as C++, and C smoke programs run under several
   renderers and sanitizers ([`c-api/COMPATIBILITY.md`](c-api/COMPATIBILITY.md),
   [`c-api/RELEASE_GATE.md`](c-api/RELEASE_GATE.md)).
3. **The binding's tests**: every import resolves; every struct layout and constant matches the
   headers; lifetime torture (create and dispose many thousands of resources, force collection,
   check for no leaks, double frees or stale handles, and a clean shutdown); and parity runs where
   the same deterministic frame, drawn through the binding and through C++, yields the same pixels,
   state, errors and input.

For XNA-compatible behaviour, compare three results where they can differ: the XNA/.NET reference,
CNA's native result (which may come through Sharp Runtime), and what the binding returns. Strings,
path normalization, `TimeSpan` arithmetic, exception categories, formatting and collection order are
where Sharp Runtime, CNA and marshalling each introduce their own differences, and the C# side can
check against the real .NET base class library directly.

When something goes wrong, the layers also tell you where to fix it:

| Symptom | Usually owned by |
|---|---|
| A native string is formatted wrongly by a `System::` type | Sharp Runtime |
| A content failure surfaces as the wrong managed exception | the binding's error mapping, or the C ABI's result category |
| A texture is destroyed twice after `Dispose()` | the binding's ownership code, or the ABI's handle checks |
| An XNA game renders differently because blend semantics differ | CNA's graphics implementation |
| The application cannot find the native library | the binding's packaging |

CNA owns XNA semantics, native behaviour, C ABI stability and the conversion of native types; a
binding owns language idioms, marshalling, resource wrappers, packaging and runtime integration.

## The first program

The first milestone for any binding is not "500 classes bound". It is a small, real game running
through the whole stack — language → binding → C ABI → C++ → a CNA renderer → a window — that
clears the screen, loads a texture through `ContentManager`, draws it with `SpriteBatch`, reads the
keyboard and shuts down cleanly. In C#, unchanged XNA code:

```csharp
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

public sealed class Game1 : Game
{
    private readonly GraphicsDeviceManager _graphics;
    private SpriteBatch _spriteBatch = null!;
    private Texture2D _texture = null!;

    public Game1()
    {
        _graphics = new GraphicsDeviceManager(this);
        Content.RootDirectory = "Content";
    }

    protected override void LoadContent()
    {
        _spriteBatch = new SpriteBatch(GraphicsDevice);
        _texture = Content.Load<Texture2D>("eggbert");
    }

    protected override void Update(GameTime gameTime)
    {
        if (Keyboard.GetState().IsKeyDown(Keys.Escape))
        {
            Exit();
        }
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(Color.CornflowerBlue);

        _spriteBatch.Begin();
        _spriteBatch.Draw(_texture, new Vector2(100, 100), Color.White);
        _spriteBatch.End();
    }
}
```

Small as it is, this exercises string conversion (`RootDirectory`, the asset name), native content
loading and its error path, resource handles, the callback lifecycle, `GameTime` as a value and an
input snapshot. CNA.NET's `samples/HelloGame` is this program. The C example,
`modules/c-api/examples/c/hello_cna.c`, is the same vertical slice without `ContentManager` and
input: it creates and fills its texture itself, and shows capability queries, the string protocol
and error diagnostics instead. Prove the program on more than one operating system and renderer
before broadening the API, and use real games to find ABI design mistakes before hundreds of
classes depend on them.

## When a binding counts as supported

Compiling is not enough. A binding can be called supported when it has:

1. an ABI version check at initialization with a clear error on mismatch, and automated ABI tests
   (imports, layouts, constants);
2. correct strings, errors, handles, callbacks and shutdown, with resource-lifetime tests;
3. native library loading from its normal package on each platform it claims;
4. the [first program](#the-first-program) running, and at least one real non-trivial sample;
5. documentation of installation, its XNA-compatibility level and its platform limits, stated as
   measured;
6. a maintainer responsible for releases and for following CNA's ABI.

An XNA-compatible facade additionally needs public-API parity tests against the XNA 4.0 reference,
at least one existing XNA project that recompiles, and behaviour comparisons. A tool that reaches
into CNA's C++ or Sharp Runtime internals can be useful, but it is not a binding and must not be
presented as one.

A new binding starts as an idea and becomes a project only with a maintainer, a real user and the
above. Every binding multiplies package releases, CI jobs, platform testing, documentation and
version tracking; the archived bindings in `cna-lab` are what that cost looks like when nobody
carries it.

## XNA compatibility claims

CNA.NET's goal is **source compatibility**: an XNA 4.0 game recompiled against `CNA.XnaCompat` with
few or no source changes, matching XNA's **API** (namespaces, types, members, overloads,
exceptions) and its **behaviour** (blend, depth and sampler state, render targets, viewports,
effect parameters, matrix conventions, input timing, content, resource lifetime). Compiling is the
smallest part of that. **Binary compatibility** — running an old `Game.exe` built against
Microsoft's strong-named assemblies — is not promised.

Where compatibility gets hard:

- **The XNA Content Pipeline.** Most games load `.xnb` files. CNA reads XNB, including LZX
  compression, and has its own pipeline ([`content-pipeline.md`](content-pipeline.md)); a game's
  custom `ContentTypeReader` can be adapted when its source exists.
- **XACT and historical services.** `AudioEngine`/`WaveBank`/`SoundBank`/`Cue`, and APIs that
  depended on Xbox Live or Games for Windows Live, get a compatible surface over a local
  implementation, a replacement service, or a documented refusal.
- **What CNA does not control**: Windows Forms, `Microsoft.Win32`, `user32.dll` P/Invoke,
  DirectInput and third-party native libraries. Pure managed libraries usually work on modern
  .NET; ones tied to XNA's internals need adapting.

So never claim that every XNA game, binary or custom pipeline extension works, that every Windows
dependency becomes portable, that FFI is free, or that every CNA API exists in every language.
Publish what was measured: API coverage, tested classes and methods, sample games that pass, and
known content and behaviour deviations. Where FNA and XNA disagree, CNA follows XNA (`CLAUDE.md`),
and the divergences bindings measured are listed in
[`../plans/plan_bindings_upstream.md`](../plans/plan_bindings_upstream.md).

XNA reference material — decompiled behaviour, the reference assemblies a parity tool compares
against — is used only as the project's legal policy allows. Microsoft's binaries are never
committed or shipped. A tool that needs them asks the user to supply legally obtained copies, as
CNA.NET's API verification does.

## Further reading

- [`c-api/README.md`](c-api/README.md) — the C API: purpose, coverage, naming, hard boundaries.
- [`c-api/CONSUMING.md`](c-api/CONSUMING.md) — building a program against an installed CNA.
- [`c-api/LIMITATIONS.md`](c-api/LIMITATIONS.md) — what the ABI does not cover yet.
- [`../plans/plan_binding.md`](../plans/plan_binding.md) — the C ABI implementation plan and its
  release gate.
- [`../plans/plan_bindings_upstream.md`](../plans/plan_bindings_upstream.md) — CNA defects the
  language bindings found.
- [`libcna/cna-dotnet`](https://github.com/libcna/cna-dotnet) — CNA.NET, its
  `docs/architecture.md` and its ABI admission policy (`docs/native-abi-compatibility.md`).
