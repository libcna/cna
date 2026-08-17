# CNA Language Bindings and Stable C ABI — Consolidated Analysis

**Status:** Consolidated design analysis  
**Date:** 2026-08-14  
**Primary project:** `openeggbert/cna`  
**Purpose:** Preserve the complete useful conclusions from the discussion about exposing CNA to C#, C, JavaScript/TypeScript, Rust, Python, Java, Zig, Go, Swift, and potentially other languages, while keeping the existing C++ CNA implementation as the canonical engine.

---

## 1. Executive summary

The strongest language-expansion strategy for CNA is **not** to rewrite CNA separately in C#, Rust, Java, JavaScript, Python, or other languages.

The recommended architecture is:

```text
Language application
        ↓
Language-specific high-level binding
        ↓
Language-specific low-level interop layer
        ↓
Stable CNA C ABI
        ↓
Canonical CNA C++ implementation
        ↓
Existing CNA subsystems and renderers
```

The key architectural decision is:

> **The stable C ABI should live directly inside the main `openeggbert/cna` repository.**

Language bindings should normally live in separate repositories:

```text
openeggbert/cna          ← canonical C++ engine + canonical C ABI
openeggbert/cna-dotnet   ← C#/.NET binding
openeggbert/cna-js       ← JavaScript/TypeScript binding
openeggbert/cna-rs       ← Rust binding
openeggbert/cna-python   ← Python binding
openeggbert/cna-java     ← Java binding
openeggbert/cna-zig      ← Zig binding
openeggbert/cna-go       ← Go binding
openeggbert/cna-swift    ← Swift binding
```

Do **not** create a separate `cna-c` repository merely for the C ABI. The C ABI is not just another language binding. It is the official interoperability boundary of CNA itself and should evolve atomically with the C++ implementation.

The first binding should be **C#/.NET** because XNA itself was a C# framework. A high-quality `CNA.XnaCompat` layer could make many existing XNA 4.0 games source-compatible with CNA, allowing their C# game logic to remain C# while using the C++ CNA runtime internally.

The second most strategically interesting binding is **JavaScript/TypeScript**, especially through WebAssembly in the browser.

Rust and Python are strong later candidates. Java, Zig, Go, Swift, and other bindings should follow only after the C ABI has been proven and stabilized by real applications.

---

# 2. Why bindings are strategically important for CNA

Without bindings, CNA primarily addresses:

```text
C++ developers
+
XNA developers willing to port their code to C++
```

With a stable C ABI and language bindings, the potential audience becomes:

```text
C++
+
C# / .NET / XNA / MonoGame / FNA developers
+
JavaScript / TypeScript developers
+
Rust developers
+
Python developers
+
Java developers
+
Zig developers
+
Go developers
+
Swift developers
+
other FFI-capable languages
```

This can dramatically expand CNA's reach because the largest barrier for many developers is not the graphics API or renderer support but the requirement to use C++.

The key economic advantage is that there is still only **one engine implementation**:

```text
CNA C++
```

All language frontends share:

- the same graphics implementation,
- the same renderers,
- the same audio system,
- the same content/runtime behavior,
- the same platform layer,
- the same bug fixes,
- the same XNA compatibility work.

If a Vulkan bug is fixed in C++ CNA, every binding benefits automatically.

That is fundamentally different from maintaining:

```text
CNA C++
CNA C#
CNA Rust
CNA Java
CNA JavaScript
...
```

as independent engine implementations.

---

# 3. Current CNA repository structure and why it supports this plan

At the time of this analysis, CNA is already a physically modularized C++ monorepo.

The repository contains subsystem modules such as:

```text
modules/core
modules/math
modules/runtime
modules/graphics
modules/input
modules/audio
modules/media
modules/content
modules/storage
modules/devices
modules/devices-ext
modules/graphics-ext
modules/gamer-services
modules/net
```

Renderer implementations are also physically separated.

The current repository documentation states that there are **42 renderer implementation families exposing 46 public renderer identities**.

Examples include:

```text
Vulkan
DirectX variants
OpenGL variants
OpenGL ES
WebGL1
WebGL2
WebGPU
Metal
SDL GPU
SDL Renderer
FNA3D
Skia
Software
HTML DOM
SVG DOM
OpenVG
and many others
```

This modular structure makes a C ABI module a natural addition rather than an architectural exception.

Recommended addition:

```text
openeggbert/cna
└── modules/
    └── c-api/
        ├── CMakeLists.txt
        ├── include/
        ├── src/
        ├── tests/
        └── examples/        # only if useful and consistent with CNA module rules
```

The C ABI should depend on the normal CNA C++ modules and adapt them for foreign-language interoperability.

---

# 4. Recommended overall repository architecture

## 4.1 Main CNA repository

```text
openeggbert/cna
│
├── modules/
│   ├── core/
│   ├── math/
│   ├── runtime/
│   ├── graphics/
│   ├── input/
│   ├── audio/
│   ├── media/
│   ├── content/
│   ├── storage/
│   ├── devices/
│   ├── graphics-ext/
│   ├── gamer-services/
│   ├── net/
│   ├── renderers/
│   │   └── ...
│   │
│   └── c-api/
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── CNA/
│       │       └── C/
│       │           ├── cna.h
│       │           ├── core.h
│       │           ├── runtime.h
│       │           ├── graphics.h
│       │           ├── input.h
│       │           ├── audio.h
│       │           ├── content.h
│       │           ├── media.h
│       │           └── ...
│       ├── src/
│       └── tests/
│
└── docs/
    └── c-api/
```

The exact header organization can evolve, but the principle is important:

> The C ABI is part of CNA itself.

---

## 4.2 Binding repositories

Recommended eventual ecosystem:

```text
openeggbert/cna
openeggbert/cna-dotnet
openeggbert/cna-js
openeggbert/cna-rs
openeggbert/cna-python
openeggbert/cna-java
openeggbert/cna-zig
openeggbert/cna-go
openeggbert/cna-swift
```

However, **do not create all of these immediately**.

Recommended implementation order:

```text
1. CNA C ABI in openeggbert/cna
2. cna-dotnet
3. prove and stabilize ABI with real C# games
4. cna-js
5. cna-rs
6. cna-python
7. additional bindings only when justified by real demand
```

The exact ordering after .NET may change, but C# should be first.

---

# 5. Why the C ABI belongs in the main CNA repository

A separate `cna-c` repository is not recommended.

The reason is that the C ABI is not an optional adapter owned by an external language ecosystem. It is the stable interoperability contract of the canonical engine.

Suppose CNA changes:

```cpp
Texture2D
```

A corresponding ABI change may need to happen simultaneously:

```c
cna_texture2d_*
```

With the C ABI inside the same repository, one pull request can modify:

```text
modules/graphics/...
modules/c-api/...
tests/...
```

atomically.

With a separate repository:

```text
CNA change
    ↓
release
    ↓
cna-c notices incompatibility
    ↓
second update
    ↓
temporary version mismatch
```

This would create unnecessary synchronization work.

Therefore:

```text
CNA C++ API       → main cna repo
CNA C ABI         → main cna repo
C# binding        → cna-dotnet repo
Rust binding      → cna-rs repo
JS binding        → cna-js repo
etc.
```

---

# 6. The C ABI should be treated as a first-class public API

CNA would effectively expose two native interfaces:

```text
CNA C++ API
+
CNA stable C ABI
```

C++ users use the normal public C++ headers.

For example:

```cpp
#include <Microsoft/Xna/Framework/Game.hpp>
```

Binding authors and C users use:

```c
#include <CNA/C/cna.h>
```

Suggested documentation wording:

```text
C++ API
    Primary idiomatic C++ programming interface.

C ABI
    Stable native interoperability interface intended for C and
    foreign-function bindings.
```

The C ABI should be designed deliberately for long-term stability and should **not** simply expose every C++ implementation detail.

---

# 7. Do not expose the C++ ABI directly to language bindings

Avoid architectures such as:

```text
C#
 ↓
direct arbitrary C++ classes
```

The C++ ABI contains too many unstable or compiler-specific constructs:

```text
std::string
std::vector<T>
templates
C++ inheritance
virtual methods
C++ exceptions
RTTI
name mangling
compiler-specific layouts
standard-library ABI differences
```

Instead:

```text
C++
 ↓
stable C ABI
 ↓
language FFI
```

The C boundary should use only predictable ABI-friendly constructs such as:

```text
fixed-width integers
float / double
plain structs
opaque handles
pointer + length
explicit UTF-8
explicit error codes
callback function pointers
```

---

# 8. Basic C ABI design

A possible low-level style:

```c
typedef uint64_t CNA_Handle;

typedef struct CNA_Vector2 {
    float x;
    float y;
} CNA_Vector2;

typedef struct CNA_Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} CNA_Color;

typedef enum CNA_Result {
    CNA_OK = 0,
    CNA_ERROR_INVALID_ARGUMENT = 1,
    CNA_ERROR_INVALID_HANDLE = 2,
    CNA_ERROR_OUT_OF_MEMORY = 3,
    CNA_ERROR_IO = 4,
    CNA_ERROR_INTERNAL = 5
} CNA_Result;
```

Resource API:

```c
CNA_Result cna_texture2d_create(
    CNA_Handle graphics_device,
    int32_t width,
    int32_t height,
    CNA_Handle* out_texture);

void cna_texture2d_release(
    CNA_Handle texture);

int32_t cna_texture2d_get_width(
    CNA_Handle texture);

int32_t cna_texture2d_get_height(
    CNA_Handle texture);
```

The C ABI does not need to mirror C++ inheritance.

For example, if C++ has:

```cpp
class Texture2D : public GraphicsResource
```

the C API can simply expose:

```text
CNA_Texture2D handle
+
texture-specific functions
```

---

# 9. Prefer opaque handles over exposing raw C++ pointers

A C# object might internally contain:

```text
Texture2D C# wrapper
    ↓
handle = 0x000000070000007D
    ↓
CNA handle table
    ↓
C++ Texture2D
```

A robust handle can encode:

```text
32-bit slot index
+
32-bit generation
```

Example:

```text
index = 125
generation = 7
```

After the native resource is destroyed, the slot generation changes:

```text
index = 125
generation = 8
```

A stale managed handle containing `125/7` is then detectable and cannot silently reference an unrelated new object.

This helps catch:

- use-after-free,
- double release,
- stale references,
- invalid cross-language resource reuse.

Raw pointers can sometimes be acceptable internally, but generation-checked handles are safer as a general language boundary.

---

# 10. Never let C++ exceptions cross the ABI

Bad:

```cpp
extern "C" void cna_load_texture(...) {
    Texture2D::Load(...); // may throw
}
```

Recommended:

```cpp
extern "C" CNA_Result cna_load_texture(...) {
    try {
        // native CNA operation
        return CNA_OK;
    }
    catch (const std::exception& ex) {
        cna_set_last_error(ex.what());
        return CNA_ERROR_INTERNAL;
    }
    catch (...) {
        cna_set_last_error("Unknown native exception");
        return CNA_ERROR_INTERNAL;
    }
}
```

Then the language binding maps:

```text
C++ exception
    ↓
CNA_Result + error details
    ↓
language-specific exception/error
```

For C#:

```csharp
var result = Native.cna_load_texture(...);

if (result != CnaResult.Ok)
    throw CnaNativeException.FromLastError();
```

No C++ exception should ever escape through the C ABI.

---

# 11. Strings should have one explicit encoding

Use UTF-8.

For example:

```c
CNA_Result cna_window_set_title(
    CNA_Handle window,
    const char* utf8,
    size_t byte_length);
```

Avoid ambiguous platform-specific string handling.

The ABI should not depend on:

```text
wchar_t
locale-dependent narrow strings
platform-specific encodings
```

High-level bindings convert their native string representation to/from UTF-8.

---

# 12. Avoid ambiguous ABI primitive types

Prefer explicit fixed-width integer types:

```text
int32_t
uint32_t
int64_t
uint64_t
```

Use explicit representations for booleans, for example:

```text
uint8_t
```

or:

```text
int32_t
```

rather than exposing C++ `bool` where ABI/marshalling ambiguity is undesirable.

Be careful with:

```text
long
unsigned long
size_t
wchar_t
```

when their size or representation would become part of the public binary contract.

---

# 13. Collections should not cross as `std::vector`

Do not expose:

```cpp
std::vector<DisplayMode>
```

through the public ABI.

Possible C pattern:

```c
size_t cna_display_modes_count(
    CNA_Handle graphics_adapter);

CNA_Result cna_display_modes_copy(
    CNA_Handle graphics_adapter,
    CNA_DisplayMode* buffer,
    size_t capacity,
    size_t* written);
```

Or use pointer-plus-count conventions where ownership is unambiguous.

High-level bindings can then expose:

```csharp
IReadOnlyList<DisplayMode>
```

```rust
Vec<DisplayMode>
```

```python
list[DisplayMode]
```

etc.

---

# 14. ABI versioning must be explicit

The C ABI should be versioned independently from the C++ project version.

Example:

```c
uint32_t cna_get_abi_version(void);
```

Possible encoding:

```text
major = 1
minor = 4
```

A binding should check compatibility during initialization.

Example C# logic:

```csharp
var abi = Native.GetAbiVersion();

if (abi.Major != ExpectedAbiMajor)
    throw new CnaAbiMismatchException(...);
```

Configuration/input structs can use:

```c
typedef struct CNA_GameCreateInfo {
    uint32_t struct_size;
    uint32_t struct_version;

    int32_t width;
    int32_t height;

    /* future fields appended here */
} CNA_GameCreateInfo;
```

This allows newer CNA versions to identify which fields an older binding knows.

The C ABI should aim for strong compatibility within one ABI major version.

---

# 15. Binding versions do not need to equal CNA versions

Avoid requiring:

```text
CNA       3.7
CNA.NET   3.7
cna-rs    3.7
cna-js    3.7
```

They are different products with different release cadences.

A better relationship is:

```text
CNA.NET 0.6 requires CNA ABI >= 1.3 and < 2.0
cna-rs 0.4 requires CNA ABI >= 1.2 and < 2.0
```

Example possible versions:

```text
CNA C++       0.8.4
CNA C ABI     1.3
CNA.NET       0.5
cna-js        0.2
cna-rs        0.2
```

The important compatibility contract is with the ABI version.

---

# 16. Machine-readable ABI metadata and code generation

Do not manually duplicate the entire ABI definition separately in every language.

Otherwise the ecosystem will eventually drift:

```text
C++ says enum value = 16
C# says enum value = 15
Rust says enum value = 14
```

There should be one canonical ABI definition.

Possible design:

```text
openeggbert/cna
└── modules/c-api/
    └── api/
        ├── core.yaml
        ├── runtime.yaml
        ├── graphics.yaml
        ├── input.yaml
        ├── audio.yaml
        └── content.yaml
```

YAML is only an example. Alternatives include:

- JSON,
- TOML,
- a custom schema,
- annotated C headers,
- Clang AST processing.

From this canonical description, tools can generate or verify:

```text
C declarations
C++ adapter declarations
C# LibraryImport declarations
Rust extern declarations
Java native declarations
TypeScript/WASM declarations
documentation
enum maps
ABI tests
parity reports
```

Do not attempt a naive:

> "Translate every C++ header automatically into every target language."

The C++ public API contains too many concepts that should be adapted rather than mechanically copied.

A curated ABI schema is more maintainable.

---

# 17. C#/.NET is the highest-priority binding

C# is not merely another language for CNA.

XNA itself was a C# framework.

Therefore a C# CNA frontend has unique strategic value:

```text
original XNA C# game
        ↓
CNA.XnaCompat
        ↓
CNA.Interop
        ↓
CNA C ABI
        ↓
CNA C++
```

The game can potentially remain C#.

This is dramatically more attractive than requiring every XNA game to be manually rewritten into C++.

---

# 18. Recommended `cna-dotnet` repository structure

```text
openeggbert/cna-dotnet
│
├── src/
│   ├── CNA.Interop/
│   ├── CNA.Framework/
│   └── CNA.XnaCompat/
│
├── tests/
│   ├── CNA.Interop.Tests/
│   ├── CNA.Framework.Tests/
│   └── CNA.XnaCompat.Tests/
│
├── samples/
├── tools/
│   └── binding-generator/
│
└── CNA.sln
```

Recommended layer responsibilities:

```text
CNA.XnaCompat
    ↓
CNA.Framework
    ↓
CNA.Interop
    ↓
CNA C ABI
    ↓
CNA C++
```

### `CNA.Interop`

Internal, low-level native calls:

```csharp
internal static unsafe partial class Native
{
    [LibraryImport("cna-native")]
    internal static partial CnaResult
        cna_graphics_device_clear(
            nint device,
            NativeColor color);
}
```

### `CNA.Framework`

Idiomatic public CNA .NET API.

### `CNA.XnaCompat`

Maximum practical XNA 4.0 source-compatibility facade using familiar namespaces such as:

```csharp
Microsoft.Xna.Framework
Microsoft.Xna.Framework.Graphics
Microsoft.Xna.Framework.Input
Microsoft.Xna.Framework.Audio
Microsoft.Xna.Framework.Content
```

This split prevents all future CNA functionality from being permanently constrained by historical XNA naming.

---

# 19. C# high-level API should look almost like real XNA

Target example:

```csharp
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

public sealed class Game1 : Game
{
    private GraphicsDeviceManager graphics;
    private SpriteBatch spriteBatch = null!;
    private Texture2D texture = null!;

    public Game1()
    {
        graphics = new GraphicsDeviceManager(this);
        Content.RootDirectory = "Content";
    }

    protected override void LoadContent()
    {
        spriteBatch = new SpriteBatch(GraphicsDevice);
        texture = Content.Load<Texture2D>("player");
    }

    protected override void Update(GameTime gameTime)
    {
        if (Keyboard.GetState().IsKeyDown(Keys.Escape))
            Exit();

        base.Update(gameTime);
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(Color.CornflowerBlue);

        spriteBatch.Begin();
        spriteBatch.Draw(
            texture,
            new Vector2(100, 100),
            Color.White);
        spriteBatch.End();

        base.Draw(gameTime);
    }
}
```

Internally:

```text
Game1.cs
    ↓
CNA.XnaCompat
    ↓
CNA.Framework
    ↓
CNA.Interop
    ↓
CNA C ABI
    ↓
C++ CNA
```

---

# 20. `Game` inheritance requires a callback bridge

C# cannot directly derive from an arbitrary native C++ class through ordinary P/Invoke.

The recommended design is a native adapter.

Conceptually:

```cpp
class ManagedGameAdapter final : public Game {
public:
    ManagedGameAdapter(
        CNA_ManagedGameCallbacks callbacks,
        void* context)
        : callbacks_(callbacks),
          context_(context) {}

protected:
    void Initialize() override {
        callbacks_.initialize(context_);
    }

    void Update(GameTime game_time) override {
        CNA_GameTime native_time = convert(game_time);
        callbacks_.update(context_, &native_time);
    }

    void Draw(GameTime game_time) override {
        CNA_GameTime native_time = convert(game_time);
        callbacks_.draw(context_, &native_time);
    }

private:
    CNA_ManagedGameCallbacks callbacks_;
    void* context_;
};
```

Architecture:

```text
C# Game1
    ↓ callback table
C++ ManagedGameAdapter
    ↓ normal C++ inheritance
C++ CNA Game
```

Native CNA can retain ownership of:

- window creation,
- SDL/platform event pumping,
- timing,
- frame lifecycle,
- graphics initialization,
- renderer selection,
- platform integration.

It invokes managed callbacks for:

```text
Initialize
LoadContent
Update
Draw
UnloadContent
shutdown-related hooks
```

Modern .NET can expose static unmanaged-callable functions through native function pointers.

---

# 21. Keep cross-language callback frequency low

Avoid a design where control repeatedly ping-pongs across the FFI boundary thousands of times unnecessarily:

```text
C++ → C# → C++ → C# → C++ → C# ...
```

A frame should have coarse callback boundaries.

For example:

```text
C++ CNA
    poll platform events
    prepare frame

        ↓ callback

C# Update()

        ↓ selected native calls

C++ CNA

        ↓ callback

C# Draw()

        ↓ graphics/native calls

C++ CNA
    present frame
```

A handful of managed/native callbacks per frame is reasonable.

High-volume commands should be batchable.

---

# 22. `SpriteBatch` should support batching across the FFI boundary

The simplest first implementation can call native `SpriteBatch.Draw()` for each C# call.

However, high draw counts may make interop overhead important.

A later optimization:

```text
C# SpriteBatch.Draw()
C# SpriteBatch.Draw()
C# SpriteBatch.Draw()
...
        ↓
managed draw-command buffer
        ↓
SpriteBatch.End()
        ↓
one or a few native calls
        ↓
C++ SpriteBatch
```

Example native command:

```c
typedef struct CNA_SpriteDrawCommand {
    CNA_Handle texture;
    CNA_Vector2 position;
    CNA_Rect source;
    CNA_Color color;
    float rotation;
    CNA_Vector2 origin;
    CNA_Vector2 scale;
    int32_t effects;
    float layer_depth;
} CNA_SpriteDrawCommand;
```

Possible API:

```c
CNA_Result cna_sprite_batch_draw_many(
    CNA_Handle sprite_batch,
    const CNA_SpriteDrawCommand* commands,
    size_t command_count);
```

The public C# API remains idiomatic:

```csharp
spriteBatch.Draw(...);
```

while the binding can optimize transfer internally.

The same optimization can later benefit JS/WASM and other languages.

---

# 23. Math/value types should usually be native-language implementations

Not every XNA type needs to be backed by a C++ object.

Types such as:

```text
Vector2
Vector3
Vector4
Matrix
Quaternion
Color
Rectangle
Point
BoundingBox
BoundingSphere
Ray
Plane
GameTime-like immutable/value data
```

should generally be implemented directly in the target language.

For C#:

```csharp
[StructLayout(LayoutKind.Sequential)]
public struct Vector2
{
    public float X;
    public float Y;

    public Vector2(float x, float y)
    {
        X = x;
        Y = y;
    }

    public float Length()
        => MathF.Sqrt(X * X + Y * Y);
}
```

It would be wasteful to do:

```text
C# Vector2.Length()
    ↓ P/Invoke
C++ Vector2::Length()
```

for trivial math.

The binding should distinguish:

### Managed/local value types

```text
Vector*
Matrix
Quaternion
Color
Rectangle
Point
enums
simple immutable state structs
```

### Native-backed resource types

```text
GraphicsDevice
Texture2D
RenderTarget2D
VertexBuffer
IndexBuffer
Effect
SpriteBatch
SoundEffect
Model resources
window/platform objects
```

"Uses C++ CNA internally" does **not** mean every tiny function call must cross the FFI boundary.

---

# 24. Native resource lifetime in .NET

A managed object can wrap a native handle.

Example:

```text
C# Texture2D
    ↓
SafeHandle
    ↓
CNA native handle
    ↓
C++ Texture2D
```

Recommended managed pattern:

```csharp
internal sealed class TextureHandle : SafeHandle
{
    protected override bool ReleaseHandle()
    {
        Native.cna_texture2d_release(handle);
        return true;
    }
}
```

High-level class:

```csharp
public sealed class Texture2D : IDisposable
{
    internal TextureHandle Handle { get; }

    public void Dispose()
        => Handle.Dispose();
}
```

The binding must thoroughly test:

- normal disposal,
- forgotten disposal,
- GC interaction,
- double disposal,
- native object ownership,
- destruction after `GraphicsDevice`,
- wrong-thread destruction,
- shutdown ordering.

Resource lifetime is one of the most dangerous areas in language interoperability.

---

# 25. Input should cross the FFI boundary as snapshots

Avoid:

```text
one native call per key query
```

Prefer:

```csharp
KeyboardState state = Keyboard.GetState();

if (state.IsKeyDown(Keys.A)) ...
if (state.IsKeyDown(Keys.Space)) ...
```

`Keyboard.GetState()` performs one native snapshot copy.

Then all `IsKeyDown()` calls are managed/local.

The same pattern applies to:

```text
MouseState
GamePadState
TouchCollection
```

This is both faster and closer to XNA's conceptual API.

---

# 26. `ContentManager.Load<T>` can remain generic in C#

The native C ABI cannot directly expose C# generics.

The managed wrapper performs dispatch.

Example:

```csharp
public T Load<T>(string name)
{
    if (typeof(T) == typeof(Texture2D))
        return (T)(object)LoadTexture2D(name);

    if (typeof(T) == typeof(SoundEffect))
        return (T)(object)LoadSoundEffect(name);

    if (typeof(T) == typeof(Model))
        return (T)(object)LoadModel(name);

    throw new NotSupportedException(
        $"Unsupported content type {typeof(T)}");
}
```

Then:

```text
Content.Load<Texture2D>("player")
        ↓
C# type dispatch
        ↓
cna_content_load_texture2d(...)
        ↓
C++ CNA ContentManager
```

The public XNA-style API does not need to expose the limitation of the C ABI.

---

# 27. Effect parameters should avoid repeated string marshalling

Original XNA-style code:

```csharp
effect.Parameters["World"].SetValue(world);
```

A naive implementation could repeatedly transfer `"World"` through the ABI every frame.

A better implementation resolves the parameter once:

```text
"World"
    ↓
native lookup
    ↓
parameter ID / handle = 17
```

Then repeated updates use:

```text
SetMatrix(parameter_id = 17, matrix)
```

The managed `EffectParameter` object caches the native identity.

This principle applies to many name-based APIs.

---

# 28. Renderer selection remains native

Language bindings should not expose renderer implementation internals.

The language may offer a public enum:

```csharp
graphics.Renderer = GraphicsRenderer.Vulkan;
```

but internally this is only:

```text
managed enum
    ↓
C ABI enum
    ↓
CNA C++
    ↓
native renderer factory
```

The binding should not know about:

```text
VkDevice
ID3D12Device
MTLDevice
SDL_GPUDevice
```

unless an explicitly advanced native-extension API is later created.

This is especially important because CNA already has many renderer implementations and public identities. The bindings should inherit renderer support automatically through the core.

---

# 29. Do not use C++/CLI as the primary .NET architecture

C++/CLI can make C# ↔ C++ wrapping convenient on Windows, but it is the wrong foundation for CNA's cross-platform goals.

Preferred:

```text
C#/.NET
    ↓
P/Invoke / LibraryImport
    ↓
C ABI
    ↓
C++ CNA
```

Avoid making this the main path:

```text
C#
    ↓
C++/CLI
    ↓
C++
```

The stable C ABI is more portable and also reusable by every other language.

---

# 30. .NET native loading and packaging

A C# user should not need to:

```text
clone CNA
install CMake
build C++
find the correct .dll/.so
copy files manually
```

The desired experience is:

```bash
dotnet add package CNA.Framework
```

Potential NuGet package layout:

```text
CNA.Framework.nupkg
│
├── lib/
│   └── netX/
│       └── CNA.Framework.dll
│
└── runtimes/
    ├── win-x64/
    │   └── native/
    │       └── cna-native.dll
    ├── win-arm64/
    │   └── native/
    │       └── cna-native.dll
    ├── linux-x64/
    │   └── native/
    │       └── libcna-native.so
    ├── linux-arm64/
    │   └── native/
    │       └── libcna-native.so
    ├── osx-x64/
    │   └── native/
    │       └── libcna-native.dylib
    └── osx-arm64/
        └── native/
            └── libcna-native.dylib
```

The exact list depends on CNA-supported platforms and release policy.

The binding can use a logical native library name and map it to platform-specific binaries.

---

# 31. XNA compatibility goals for `CNA.XnaCompat`

It is critical to distinguish four kinds of compatibility.

## 31.1 Source compatibility

This should be the primary goal.

```text
XNA C# source
    ↓
recompile against CNA.XnaCompat
    ↓
CNA runtime
```

Ideal result:

```text
replace project references/package
+
few or zero source modifications
```

This is realistic and strategically valuable.

---

## 31.2 API compatibility

CNA must match:

```text
namespaces
class names
method names
properties
constructors
interfaces
enums
overloads
exceptions
public behavior
```

It is not enough to have a method named `SpriteBatch.Draw`.

The relevant XNA overload family and semantics should be present.

---

## 31.3 Behavioral compatibility

Code may compile and still behave incorrectly.

Important areas include:

```text
blend-state behavior
depth-state behavior
sampler behavior
render-target semantics
viewport behavior
effect parameter behavior
matrix conventions
texture addressing
half-pixel/history-sensitive rendering details
input timing
content behavior
resource lifetime
```

The goal should therefore be:

```text
API parity
+
behavior parity
```

not merely:

```text
compile parity
```

---

## 31.4 Binary compatibility

This is different and should not initially be promised.

A random old precompiled:

```text
Game.exe
Game.dll
```

that expects Microsoft's original XNA assemblies may have:

- assembly identity requirements,
- version requirements,
- strong-name expectations,
- old runtime requirements,
- dependencies on historical services.

The primary goal should be:

> **Source recompile compatibility, not magical drop-in execution of every existing binary.**

Binary compatibility could be investigated later as a separate project if useful.

---

# 32. Will all XNA games run?

No.

The correct claim is closer to:

> CNA.NET aims to provide source-level and behavioral compatibility with Microsoft XNA Framework 4.0, allowing existing XNA games to be recompiled against CNA with minimal or no source changes where supported.

Simple 2D XNA games are likely the easiest early targets.

Typical API set:

```text
Game
GameTime
GraphicsDeviceManager
GraphicsDevice
Texture2D
SpriteBatch
SpriteFont
Keyboard
Mouse
GamePad
SoundEffect
ContentManager
Vector2
Rectangle
Color
MathHelper
```

A high-quality implementation could make some projects close to:

```text
git clone old-XNA-game
    ↓
replace framework references
    ↓
dotnet build
    ↓
run on CNA
```

Complex 3D games are harder.

Potential APIs:

```text
Model
ModelMesh
ModelBone
VertexBuffer
IndexBuffer
VertexDeclaration
BasicEffect
Effect
EffectTechnique
EffectPass
RenderTarget2D
custom .fx shaders
OcclusionQuery
instancing
custom content processors
```

These expose deeper behavioral differences.

---

# 33. XNA Content Pipeline is a major compatibility issue

Many XNA games do not load original asset formats directly.

They use:

```text
source asset
    ↓
XNA Content Pipeline
    ↓
.xnb
```

Then:

```csharp
Texture2D texture =
    Content.Load<Texture2D>("player");
```

Good compatibility therefore requires CNA to handle relevant XNA content behavior and possibly XNB.

Important built-in content types include:

```text
Texture2D
SpriteFont
Model
SoundEffect
Effect
Song
```

Custom pipelines can be harder:

```text
ContentImporter
ContentProcessor
ContentTypeWriter
ContentTypeReader
```

If source code is available, a custom `ContentTypeReader` may be adaptable to CNA.NET.

If only an XNB exists, binary content compatibility may require format-level support.

---

# 34. XACT and historical services

Some XNA games rely on XACT-style APIs:

```text
AudioEngine
WaveBank
SoundBank
Cue
```

CNA must either implement compatible semantics, provide an adaptation path, or document unsupported cases.

Similarly, APIs tied historically to Xbox Live / Games for Windows Live cannot always be reproduced literally because the original online services may no longer exist.

Potential strategy:

```text
API-compatible surface
+
local implementation
or
replacement backend
or
well-defined stub
```

depending on functionality.

---

# 35. Windows-specific and third-party dependencies are outside CNA's control

An XNA game may additionally use:

```text
System.Windows.Forms
Microsoft.Win32
user32.dll P/Invoke
DirectInput
Windows Media
third-party native DLLs
```

CNA cannot automatically make non-XNA Windows dependencies cross-platform.

Third-party XNA libraries may also need work:

```text
physics engines
storage helpers
animation libraries
particle systems
networking helpers
custom UI libraries
```

Pure managed libraries that still run on modern .NET may work with minimal changes. Libraries deeply coupled to the original XNA implementation may require adaptation.

Therefore "XNA source compatibility" should never be marketed as "every historical XNA project works unchanged."

---

# 36. Why CNA.NET could radically reduce porting cost

Without a C# binding, porting an XNA game to CNA may mean:

```text
C# XNA game
    ↓
manual rewrite into C++
    ↓
CNA C++
```

With CNA.NET:

```text
C# XNA game
    ↓
CNA.XnaCompat
    ↓
CNA C++
```

The application's gameplay code can remain in C#.

A game with a large C# codebase no longer needs a language rewrite. Work focuses on actual API/content/behavior incompatibilities.

This can potentially reduce some ports from:

```text
thousands of hours
```

to:

```text
tens or hundreds of hours
```

depending on complexity and CNA compatibility.

This is one of the strongest strategic arguments for the .NET binding.

---

# 37. Racing Game as a potential future CNA.NET compatibility test

A large XNA Racing Game-style project is a strong test case because it exercises:

```text
3D graphics
effects/shaders
content
models
audio
input
game loop
render targets
XNA semantics
```

Instead of porting all of its C# logic to C++, a mature CNA.NET could allow:

```text
Racing Game C# source
    ↓
CNA.XnaCompat
    ↓
CNA C++
```

This does not eliminate all compatibility work, but it changes the economics of the port dramatically.

A mature real game should therefore be used as a compatibility oracle after simpler sample games work.

---

# 38. First .NET milestone

Do **not** begin by binding the entire CNA API.

The first milestone should be a tiny but complete C# game:

```csharp
public sealed class HelloGame : Game
{
    private GraphicsDeviceManager graphics;
    private SpriteBatch spriteBatch = null!;
    private Texture2D texture = null!;

    public HelloGame()
    {
        graphics = new GraphicsDeviceManager(this);
    }

    protected override void LoadContent()
    {
        spriteBatch = new SpriteBatch(GraphicsDevice);
        texture = Content.Load<Texture2D>("eggbert");
    }

    protected override void Update(GameTime gameTime)
    {
        if (Keyboard.GetState().IsKeyDown(Keys.Escape))
            Exit();
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(Color.CornflowerBlue);

        spriteBatch.Begin();
        spriteBatch.Draw(
            texture,
            new Vector2(100, 100),
            Color.White);
        spriteBatch.End();
    }
}
```

The full stack must work:

```text
C#
↓
CNA.XnaCompat / CNA.Framework
↓
CNA.Interop
↓
CNA C ABI
↓
C++ Game
↓
C++ GraphicsDevice
↓
C++ SpriteBatch
↓
selected CNA renderer
↓
window
```

Prove it on at least:

```text
Linux
Windows
```

and ideally with more than one renderer.

Once this works reliably, the architecture has passed its first serious validation.

---

# 39. Suggested `.NET` effort estimates

These are rough AI-agent-work estimates, not guarantees.

| Area | Approx. AI-agent hours |
|---|---:|
| ABI architecture and conventions | 100–250 |
| Base `libcna-c` infrastructure | 150–400 |
| .NET interop infrastructure | 100–250 |
| Game/runtime bridge | 150–350 |
| Value types/math | 100–250 |
| Input | 80–200 |
| Basic graphics/resources | 300–700 |
| SpriteBatch + textures | 200–500 |
| 3D/resources/effects | 400–1,200 |
| Audio/content | 200–600 |
| Advanced API coverage | 300–1,000 |
| Packaging/platforms | 200–500 |
| Test infrastructure | 300–800 |

Practical aggregate ranges:

```text
first useful C# CNA version:
~800–1,500 AI-agent hours

solid broad API:
~2,000–4,000 AI-agent hours

very broad CNA/XNA coverage:
~4,000–8,000+ AI-agent hours
```

These are dramatically smaller than a full reimplementation of CNA in another language because all major engine internals remain C++.

---

# 40. Parallelization of binding work

After the ABI foundation is stable, binding development parallelizes well.

Example workstreams:

```text
                 ABI FOUNDATION
                       │
       ┌───────────────┼────────────────┐
       ↓               ↓                ↓
    Runtime         Graphics           Input
       ↓               ↓                ↓
    Content          Audio             Tests
```

Possible agent allocation:

```text
Agent 1  → Game/runtime
Agent 2  → math/value types
Agent 3  → GraphicsDevice/resources
Agent 4  → SpriteBatch/2D
Agent 5  → 3D
Agent 6  → input
Agent 7  → audio
Agent 8  → content
Agent 9  → binding generator
Agent 10 → ABI/parity tests
```

The critical condition is that all agents follow one authoritative ABI specification.

Do not let separate agents invent incompatible ownership, error, string, callback, or handle conventions.

---

# 41. Required testing strategy

Three layers of testing are recommended.

## 41.1 Existing C++ CNA tests

Continue validating the engine itself.

## 41.2 Pure C ABI tests

Write real C programs that only include C headers:

```c
#include <CNA/C/cna.h>

int main(void)
{
    /* create game/resource, call API, destroy */
}
```

This proves the interface is actually C-compatible and not accidentally dependent on C++ ABI behavior.

## 41.3 Binding parity tests

Example:

```text
C++ test application
    ↓
render deterministic frame
    ↓
CRC / image / state result

C# equivalent application
    ↓
binding
    ↓
same C++ CNA
    ↓
same deterministic result
```

Compare:

```text
pixels
state values
resource behavior
exceptions/errors
input snapshots
content results
```

Binding-specific lifetime torture tests are essential.

Example:

```csharp
for (int i = 0; i < 100_000; i++)
{
    using var texture =
        new Texture2D(GraphicsDevice, 32, 32);
}
```

Then force GC and verify:

```text
no leaks
no double free
no stale handles
correct shutdown order
```

---

# 42. Rust binding

Recommended repository:

```text
openeggbert/cna-rs
```

Recommended structure:

```text
cna-rs/
├── cna-sys/
│   └── raw C ABI bindings
├── cna/
│   └── safe idiomatic Rust wrapper
├── examples/
└── tests/
```

Architecture:

```text
Rust application
    ↓
cna
    ↓
cna-sys
    ↓
CNA C ABI
    ↓
CNA C++
```

The Rust API should retain XNA concepts but use Rust idioms.

Example:

```rust
use cna::framework::*;
use cna::graphics::*;
use cna::input::*;

struct Game1 {
    sprite_batch: Option<SpriteBatch>,
    texture: Option<Texture2D>,
}

impl Game1 {
    fn new() -> Self {
        Self {
            sprite_batch: None,
            texture: None,
        }
    }
}

impl Game for Game1 {
    fn load_content(
        &mut self,
        ctx: &mut GameContext,
    ) -> CnaResult<()> {
        self.sprite_batch =
            Some(SpriteBatch::new(
                ctx.graphics_device())?);

        self.texture =
            Some(ctx.content()
                .load_texture2d("player")?);

        Ok(())
    }

    fn update(
        &mut self,
        ctx: &mut GameContext,
        _game_time: &GameTime,
    ) -> CnaResult<()> {
        if Keyboard::get_state()
            .is_key_down(Keys::Escape)
        {
            ctx.exit();
        }

        Ok(())
    }

    fn draw(
        &mut self,
        ctx: &mut GameContext,
        _game_time: &GameTime,
    ) -> CnaResult<()> {
        ctx.graphics_device()
            .clear(Color::CORNFLOWER_BLUE);

        let batch =
            self.sprite_batch.as_mut().unwrap();

        let texture =
            self.texture.as_ref().unwrap();

        batch.begin();

        batch.draw(
            texture,
            Vector2::new(100.0, 100.0),
            Color::WHITE,
        );

        batch.end();

        Ok(())
    }
}

fn main() -> CnaResult<()> {
    cna::run(Game1::new())
}
```

Rust ownership can make resource wrappers especially clean.

A `Texture2D` wrapper can release its native handle in `Drop`.

Do **not** rewrite CNA in Rust merely to support Rust users. A binding is far more economical.

---

# 43. Python binding

Recommended repository:

```text
openeggbert/cna-python
```

Architecture:

```text
Python
    ↓
pycna / high-level CNA Python package
    ↓
C ABI wrapper
    ↓
CNA C++
```

Potential API:

```python
from cna import (
    Game,
    GraphicsDeviceManager,
    SpriteBatch,
    Vector2,
    Color,
    Keyboard,
    Keys,
)

class Game1(Game):

    def __init__(self):
        super().__init__()

        self.graphics = GraphicsDeviceManager(self)
        self.content.root_directory = "Content"

        self.sprite_batch = None
        self.texture = None

    def load_content(self):
        self.sprite_batch =
            SpriteBatch(self.graphics_device)

        self.texture =
            self.content.load_texture2d("player")

    def update(self, game_time):
        if Keyboard.get_state().is_key_down(
            Keys.ESCAPE):
            self.exit()

    def draw(self, game_time):
        self.graphics_device.clear(
            Color.CORNFLOWER_BLUE)

        self.sprite_batch.begin()

        self.sprite_batch.draw(
            self.texture,
            Vector2(100, 100),
            Color.WHITE)

        self.sprite_batch.end()


Game1().run()
```

Python should use Pythonic properties:

```python
texture.width
texture.height
```

rather than mechanically copying Java/C++ getter naming.

Potential use cases:

```text
rapid prototyping
education
tooling
content scripts
tests
small games
debug utilities
```

---

# 44. Java binding

Recommended repository:

```text
openeggbert/cna-java
```

Architecture:

```text
Java application
    ↓
CNA Java API
    ↓
native interop
    ↓
CNA C ABI
    ↓
CNA C++
```

No additional Java-compatible runtime is required. This is simply a Java binding to the native CNA engine.

Example:

```java
import org.openeggbert.cna.framework.*;
import org.openeggbert.cna.graphics.*;
import org.openeggbert.cna.input.*;

public final class Game1 extends Game {

    private final GraphicsDeviceManager graphics;

    private SpriteBatch spriteBatch;
    private Texture2D texture;

    public Game1() {
        graphics =
            new GraphicsDeviceManager(this);

        getContent()
            .setRootDirectory("Content");
    }

    @Override
    protected void loadContent() {
        spriteBatch =
            new SpriteBatch(
                getGraphicsDevice());

        texture =
            getContent()
                .loadTexture2D("player");
    }

    @Override
    protected void update(
        GameTime gameTime)
    {
        if (Keyboard.getState()
            .isKeyDown(Keys.ESCAPE))
        {
            exit();
        }
    }

    @Override
    protected void draw(
        GameTime gameTime)
    {
        getGraphicsDevice().clear(
            Color.CORNFLOWER_BLUE);

        spriteBatch.begin();

        spriteBatch.draw(
            texture,
            new Vector2(100, 100),
            Color.WHITE);

        spriteBatch.end();
    }

    public static void main(String[] args) {
        new Game1().run();
    }
}
```

Java maps well to the object-oriented XNA model, although exact API naming should still follow Java conventions where that improves usability.

---

# 45. Zig binding

Recommended repository:

```text
openeggbert/cna-zig
```

Zig has excellent C interoperability.

The raw layer can be almost trivial:

```zig
const native = @cImport({
    @cInclude("CNA/C/cna.h");
});
```

A higher-level wrapper can provide an idiomatic API.

Example:

```zig
const cna = @import("cna");

const Game1 = struct {
    sprite_batch: ?cna.SpriteBatch = null,
    texture: ?cna.Texture2D = null,

    pub fn loadContent(
        self: *Game1,
        ctx: *cna.GameContext,
    ) !void {
        self.sprite_batch =
            try cna.SpriteBatch.init(
                ctx.graphicsDevice());

        self.texture =
            try ctx.content()
                .loadTexture2D("player");
    }

    pub fn update(
        self: *Game1,
        ctx: *cna.GameContext,
        game_time: cna.GameTime,
    ) !void {
        _ = self;
        _ = game_time;

        if (cna.Keyboard.getState()
            .isKeyDown(.escape))
        {
            ctx.exit();
        }
    }

    pub fn draw(
        self: *Game1,
        ctx: *cna.GameContext,
        game_time: cna.GameTime,
    ) !void {
        _ = game_time;

        ctx.graphicsDevice().clear(
            cna.Color.cornflower_blue);

        var batch =
            &self.sprite_batch.?;

        batch.begin();

        batch.draw(
            &self.texture.?,
            cna.Vector2.init(100, 100),
            cna.Color.white,
        );

        batch.end();
    }
};
```

Zig is technically a natural C ABI consumer, although the potential user base is smaller than C#, JS, Rust, or Python.

---

# 46. Go binding

Recommended repository if demand appears:

```text
openeggbert/cna-go
```

Go has no C#-style inheritance, so the API should adapt to Go idioms.

Example:

```go
package main

import "github.com/openeggbert/cna-go/cna"

type Game1 struct {
    SpriteBatch *cna.SpriteBatch
    Texture     *cna.Texture2D
}

func (g *Game1) LoadContent(
    ctx *cna.GameContext,
) error {
    g.SpriteBatch =
        cna.NewSpriteBatch(
            ctx.GraphicsDevice())

    texture, err :=
        ctx.Content()
            .LoadTexture2D("player")

    if err != nil {
        return err
    }

    g.Texture = texture
    return nil
}

func (g *Game1) Update(
    ctx *cna.GameContext,
    gameTime cna.GameTime,
) {
    if cna.KeyboardGetState().
        IsKeyDown(cna.KeyEscape)
    {
        ctx.Exit()
    }
}

func (g *Game1) Draw(
    ctx *cna.GameContext,
    gameTime cna.GameTime,
) {
    ctx.GraphicsDevice().
        Clear(cna.CornflowerBlue)

    g.SpriteBatch.Begin()

    g.SpriteBatch.Draw(
        g.Texture,
        cna.Vector2{X: 100, Y: 100},
        cna.White)

    g.SpriteBatch.End()
}
```

The concepts remain XNA-like even though syntax and lifecycle follow Go.

---

# 47. Swift binding

Possible repository:

```text
openeggbert/cna-swift
```

Potential API:

```swift
import CNA

final class Game1: Game {

    var graphics: GraphicsDeviceManager!
    var spriteBatch: SpriteBatch!
    var texture: Texture2D!

    override init() {
        super.init()

        graphics =
            GraphicsDeviceManager(game: self)

        content.rootDirectory = "Content"
    }

    override func loadContent() {
        spriteBatch =
            SpriteBatch(
                graphicsDevice:
                    graphicsDevice)

        texture =
            content.loadTexture2D("player")
    }

    override func update(
        _ gameTime: GameTime)
    {
        if Keyboard.getState()
            .isKeyDown(.escape)
        {
            exit()
        }
    }

    override func draw(
        _ gameTime: GameTime)
    {
        graphicsDevice.clear(
            .cornflowerBlue)

        spriteBatch.begin()

        spriteBatch.draw(
            texture,
            position: Vector2(
                x: 100,
                y: 100),
            color: .white)

        spriteBatch.end()
    }
}

Game1().run()
```

This could be useful especially on Apple platforms, assuming the native CNA platform/renderer stack supports the relevant targets well.

---

# 48. Pure C API usage

The C ABI should itself be usable directly from C.

Example:

```c
#include <CNA/C/cna.h>

static CNA_Handle texture;
static CNA_Handle sprite_batch;

void load_content(CNA_Handle game)
{
    CNA_Handle device =
        cna_game_get_graphics_device(game);

    cna_sprite_batch_create(
        device,
        &sprite_batch);

    cna_content_load_texture2d(
        cna_game_get_content(game),
        "player",
        &texture);
}

void update(
    CNA_Handle game,
    const CNA_GameTime* game_time)
{
    CNA_KeyboardState state;

    cna_keyboard_get_state(&state);

    if (cna_keyboard_state_is_key_down(
        &state,
        CNA_KEY_ESCAPE))
    {
        cna_game_exit(game);
    }
}

void draw(
    CNA_Handle game,
    const CNA_GameTime* game_time)
{
    CNA_Handle device =
        cna_game_get_graphics_device(game);

    cna_graphics_device_clear(
        device,
        CNA_COLOR_CORNFLOWER_BLUE);

    cna_sprite_batch_begin(sprite_batch);

    CNA_Vector2 position =
        {100.0f, 100.0f};

    cna_sprite_batch_draw(
        sprite_batch,
        texture,
        position,
        CNA_COLOR_WHITE);

    cna_sprite_batch_end(
        sprite_batch);
}
```

This C layer is the foundation for nearly every other binding.

---

# 49. JavaScript/TypeScript binding

JavaScript deserves special attention because there are two distinct environments:

```text
browser
Node.js
```

The browser path is especially attractive because CNA already has Emscripten/web rendering capabilities.

Recommended repository:

```text
openeggbert/cna-js
```

Recommended package name:

```text
@openeggbert/cna
```

The implementation should be **TypeScript-first** while naturally supporting normal JavaScript consumers.

---

# 50. Browser architecture: TypeScript/JavaScript → WebAssembly → CNA C++

Recommended path:

```text
TypeScript / JavaScript game
          ↓
      @openeggbert/cna
          ↓
      JS/TS wrapper
          ↓
      WebAssembly exports
          ↓
       CNA C ABI
          ↓
        CNA C++
          ↓
 WebGL2 / WebGPU / other web-capable renderer
```

CNA C++ can be compiled to WebAssembly, for example through Emscripten.

Resulting artifacts may conceptually include:

```text
cna.js
cna.wasm
```

The npm package should hide most low-level setup.

Potential user experience:

```bash
npm install @openeggbert/cna
```

Then:

```typescript
import {
    Game,
    GameTime,
    GraphicsDeviceManager,
    SpriteBatch,
    Texture2D,
    Vector2,
    Color,
    Keyboard,
    Keys
} from "@openeggbert/cna";

class Game1 extends Game {
    private graphics:
        GraphicsDeviceManager;

    private spriteBatch!:
        SpriteBatch;

    private texture!:
        Texture2D;

    constructor() {
        super();

        this.graphics =
            new GraphicsDeviceManager(this);

        this.content.rootDirectory =
            "Content";
    }

    protected loadContent(): void {
        this.spriteBatch =
            new SpriteBatch(
                this.graphicsDevice);

        this.texture =
            this.content
                .loadTexture2D("player");
    }

    protected update(
        gameTime: GameTime): void
    {
        if (Keyboard.getState()
            .isKeyDown(Keys.Escape))
        {
            this.exit();
        }
    }

    protected draw(
        gameTime: GameTime): void
    {
        this.graphicsDevice.clear(
            Color.cornflowerBlue);

        this.spriteBatch.begin();

        this.spriteBatch.draw(
            this.texture,
            new Vector2(100, 100),
            Color.white);

        this.spriteBatch.end();
    }
}

await new Game1().run();
```

---

# 51. Plain JavaScript can use the same package

Example:

```javascript
import {
    Game,
    SpriteBatch,
    Vector2,
    Color,
    Keyboard,
    Keys
} from "@openeggbert/cna";

class Game1 extends Game {

    loadContent() {
        this.spriteBatch =
            new SpriteBatch(
                this.graphicsDevice);

        this.texture =
            this.content
                .loadTexture2D("player");
    }

    update(gameTime) {
        if (Keyboard.getState()
            .isKeyDown(Keys.Escape))
        {
            this.exit();
        }
    }

    draw(gameTime) {
        this.graphicsDevice.clear(
            Color.cornflowerBlue);

        this.spriteBatch.begin();

        this.spriteBatch.draw(
            this.texture,
            new Vector2(100, 100),
            Color.white);

        this.spriteBatch.end();
    }
}

await new Game1().run();
```

TypeScript improves discoverability and static checking but does not require a separate runtime.

---

# 52. TypeScript maps naturally to XNA concepts

Many XNA concepts map cleanly:

C#:

```csharp
Texture2D texture;
Vector2 position;
Color color;
```

TypeScript:

```typescript
let texture: Texture2D;
let position: Vector2;
let color: Color;
```

C# inheritance:

```csharp
class Game1 : Game
```

TypeScript:

```typescript
class Game1 extends Game
```

Enums:

```csharp
SpriteEffects.FlipHorizontally
```

TypeScript:

```typescript
SpriteEffects.FlipHorizontally
```

Generic-looking content APIs can also be presented naturally even if runtime dispatch is implemented differently.

This makes TypeScript a particularly attractive high-level binding language.

---

# 53. JS/WASM performance: batch expensive boundary traffic

Just as with .NET, avoid extremely frequent JS ↔ WASM transitions.

A critical example is `SpriteBatch`.

Public code can remain:

```typescript
spriteBatch.begin();

for (const enemy of enemies) {
    spriteBatch.draw(
        enemy.texture,
        enemy.position,
        Color.white);
}

spriteBatch.end();
```

Internally:

```text
draw()
draw()
draw()
draw()
...
    ↓
JS command buffer
    ↓
end()
    ↓
one or a few WASM calls
    ↓
C++ CNA SpriteBatch
```

This design can dramatically reduce boundary overhead while preserving an XNA-style API.

The same native bulk-call concepts can be shared with .NET and other bindings.

---

# 54. JS math types should remain in JS/TS

Like C#, there is no reason for:

```text
Vector2.length()
```

to cross into WebAssembly.

Implement locally:

```text
Vector2
Vector3
Vector4
Matrix
Quaternion
Rectangle
Color
MathHelper
```

Native-backed:

```text
Texture2D
GraphicsDevice
RenderTarget2D
SpriteBatch
Effect
VertexBuffer
SoundEffect
Model/native resources
```

This avoids unnecessary WASM traffic.

---

# 55. JS native resource wrappers

A JS object can store a native handle:

```typescript
class Texture2D {
    #handle: bigint;

    get width(): number {
        return native.texture2DGetWidth(
            this.#handle);
    }

    dispose(): void {
        native.texture2DRelease(
            this.#handle);
    }
}
```

Conceptually:

```text
JavaScript Texture2D
    ↓
numeric handle
    ↓
WASM / C ABI handle table
    ↓
C++ Texture2D
```

Explicit resource disposal should be supported.

Automatic finalization may be a safety net but should not be the only lifetime mechanism for expensive GPU/native resources.

---

# 56. Minimal browser game target

A very attractive final experience could be:

HTML:

```html
<canvas id="game"></canvas>
<script type="module" src="/src/game.ts"></script>
```

TypeScript:

```typescript
import {
    Game,
    Color
} from "@openeggbert/cna";

class HelloGame extends Game {

    draw(): void {
        this.graphicsDevice.clear(
            Color.cornflowerBlue);
    }
}

await new HelloGame({
    canvas: "#game",
    renderer: "webgl2"
}).run();
```

Internally:

```text
TypeScript
→ WebAssembly
→ CNA C ABI
→ CNA C++
→ WEBGL2/WebGPU
→ browser canvas
```

This could substantially broaden CNA's appeal beyond C++ developers.

---

# 57. Node.js binding

Node.js can initially use the same WebAssembly build:

```text
Node.js
    ↓
cna-js
    ↓
WASM
    ↓
CNA C++
```

Advantages:

```text
one JS API
one WASM path
browser + Node sharing
simpler packaging
```

A later optional native Node path could use an N-API addon:

```text
Node.js
    ↓
N-API addon
    ↓
CNA C ABI
    ↓
native CNA C++
```

This could improve performance and desktop/native integration, but it creates per-platform native packaging complexity.

Therefore:

> Start with the WebAssembly implementation where practical. Consider native Node only after real performance or capability needs justify it.

---

# 58. Relative strategic priority of bindings

A reasonable priority order based on CNA's identity and likely impact:

```text
1. C# / .NET
2. JavaScript / TypeScript
3. Rust
4. Python
5. Java / Kotlin ecosystem
6. Zig
7. Go
8. Swift
9. additional languages only on demand
```

The order after C# is flexible.

Why C# first:

- XNA was C#.
- Existing XNA source can potentially be reused.
- It gives CNA the most direct compatibility story.

Why JS/TS is very strong:

- huge developer base,
- web/browser reach,
- natural WebAssembly path,
- TypeScript maps well to XNA's object model,
- CNA already has web renderer/platform work.

Why Rust:

- modern systems/game-development community,
- natural native-library use,
- strong ownership model for resource wrappers.

Why Python:

- scripting,
- education,
- tooling,
- prototyping,
- very low entry barrier.

---

# 59. Binding APIs should preserve concepts, not syntax at all costs

Only C# should attempt very high literal XNA API fidelity.

For other languages:

> Preserve XNA concepts and behavior, but adapt syntax to the language.

Examples:

C#:

```csharp
texture.Width
```

Python:

```python
texture.width
```

Rust:

```rust
texture.width()
```

Java:

```java
texture.getWidth()
```

Go may use:

```go
texture.Width()
```

Rust should not pretend to be C#.

Python should not expose awkward C-style getters if a property is natural.

Go should not simulate inheritance in an unnatural way.

The common conceptual model remains:

```text
Game
GraphicsDevice
Texture2D
SpriteBatch
ContentManager
Keyboard
Effect
Model
...
```

---

# 60. Same `Texture2D` concept across languages

## C#

```csharp
using var texture =
    new Texture2D(
        GraphicsDevice,
        1024,
        1024);

texture.SetData(pixels);

Console.WriteLine(
    texture.Width);
```

## Rust

```rust
let mut texture =
    Texture2D::new(
        device,
        1024,
        1024)?;

texture.set_data(&pixels)?;

println!(
    "{}",
    texture.width());
```

## Java

```java
try (Texture2D texture =
        new Texture2D(
            device,
            1024,
            1024)) {

    texture.setData(pixels);

    System.out.println(
        texture.getWidth());
}
```

## Python

```python
texture = Texture2D(
    device,
    1024,
    1024)

texture.set_data(pixels)

print(texture.width)
```

## Zig

```zig
var texture =
    try cna.Texture2D.init(
        device,
        1024,
        1024);

defer texture.deinit();

try texture.setData(pixels);

std.debug.print(
    "{}\n",
    .{texture.width()});
```

All wrappers ultimately refer to the same native concept:

```text
binding object
    ↓
CNA handle
    ↓
C++ Texture2D
```

---

# 61. Same XNA 3D concept across languages

Original C# style:

```csharp
foreach (ModelMesh mesh in model.Meshes)
{
    foreach (BasicEffect effect
             in mesh.Effects)
    {
        effect.World = world;
        effect.View = view;
        effect.Projection = projection;
    }

    mesh.Draw();
}
```

Rust:

```rust
for mesh in model.meshes() {
    for effect in mesh.basic_effects() {
        effect.set_world(world);
        effect.set_view(view);
        effect.set_projection(projection);
    }

    mesh.draw()?;
}
```

Java:

```java
for (ModelMesh mesh :
        model.getMeshes()) {

    for (BasicEffect effect :
            mesh.getBasicEffects()) {

        effect.setWorld(world);
        effect.setView(view);
        effect.setProjection(
            projection);
    }

    mesh.draw();
}
```

Python:

```python
for mesh in model.meshes:

    for effect in mesh.basic_effects:
        effect.world = world
        effect.view = view
        effect.projection = projection

    mesh.draw()
```

The syntax varies, but the mental model remains XNA/CNA.

---

# 62. Full Rust reimplementation of CNA is possible but not recommended for language support

It is technically possible to port CNA itself completely to Rust or another language.

However, this would mean maintaining another engine implementation:

```text
CNA C++
+
CNA Rust
```

Every new feature or renderer fix might need duplication.

A full parity port of a large framework can easily require tens of thousands of agent-hours.

Very rough scale discussed:

```text
Rust bindings:
~500–2,000 h for lower-level coverage
or
~1,500–4,000 h for a polished idiomatic binding,
depending on breadth

partial native Rust reimplementation:
~10,000–25,000 h

full feature-parity reimplementation:
~30,000–80,000+ h
```

These are broad conceptual estimates, not schedules.

Therefore:

> Keep C++ CNA canonical. Use the C ABI to support Rust.

The same reasoning applies to complete rewrites in other languages.

---

# 63. Why bindings can expand CNA more than another renderer

Once CNA already supports a broad renderer matrix, another exotic renderer may increase technical coverage but may not greatly increase the number of developers who can realistically use CNA.

A high-quality C# binding can directly unlock:

```text
XNA developers
MonoGame developers
FNA developers
general .NET game developers
existing C# XNA codebases
```

A JS/TS binding can unlock:

```text
browser developers
web game developers
TypeScript developers
interactive web demos
education
browser-based samples
```

Therefore multi-language accessibility may eventually provide more adoption value than some additional renderer implementations.

This is a strategic argument, not a requirement to build every binding.

---

# 64. Bindings create a possible CNA ecosystem/platform

With mature bindings:

```text
                        CNA C++
                           │
                        C ABI
                           │
          ┌────────────────┼─────────────────┐
          ↓                ↓                 ↓
       CNA.NET           cna-rs           cna-js
          ↓                ↓                 ↓
     C# XNA games       Rust tools       browser games
          │
          ├────────────┐
          ↓            ↓
       Python         Java
       tooling        apps/games
```

Possible ecosystem examples:

```text
game written in C#
editor/tooling written in Python
server/tool written in Rust
browser demo written in TypeScript
```

All can rely on one CNA engine/runtime model.

This is the point at which CNA begins to resemble a platform rather than merely a C++ library.

---

# 65. Documentation implications

Multi-language support should not create completely unrelated documentation silos.

A concept-oriented documentation system can contain language tabs/examples:

```text
Creating a Texture2D
    ├── C++
    ├── C#
    ├── TypeScript
    ├── Rust
    ├── Python
    └── Java
```

The concept remains the same.

Each language binding should still have its own package/setup documentation because installation and lifecycle differ.

Important binding documentation topics:

```text
installation
native binary loading
resource lifetime
threading rules
supported platforms
renderer selection
content loading
FFI limitations
error handling
XNA compatibility level
unsupported APIs
performance guidance
```

---

# 66. The binding project must not become another uncontrolled mega-project

There are infinitely many possible language bindings.

The existence of a technically valid binding idea does **not** mean it belongs on the active roadmap.

Recommended rule:

> A new binding is initially only an idea. It becomes an active project only after the C ABI is stable enough and there is a clear user or strategic benefit.

Do not immediately create:

```text
cna-dotnet
cna-js
cna-rs
cna-python
cna-java
cna-zig
cna-go
cna-swift
cna-lua
cna-ruby
...
```

The maintenance cost would explode.

First prove the architecture with .NET.

---

# 67. Suggested implementation roadmap

## Phase 0 — Specification

Define:

```text
C ABI design rules
handle model
ownership
threading
error model
UTF-8 convention
callback convention
ABI versioning
struct versioning
enum stability
bulk transfer conventions
code generation strategy
```

Do not begin broad automated binding generation before these rules exist.

---

## Phase 1 — Minimal C ABI

Implement enough to support:

```text
Game lifecycle
GraphicsDevice
renderer selection
Texture2D
SpriteBatch
Keyboard
basic ContentManager
basic audio if needed
```

Add pure C tests.

---

## Phase 2 — Minimal `cna-dotnet`

Implement:

```text
CNA.Interop
CNA.Framework
initial CNA.XnaCompat
```

Run a real C# game loop with:

```text
clear screen
load texture
draw texture
read keyboard
exit
```

---

## Phase 3 — Stabilize through real usage

Use:

```text
small XNA samples
2D XNA games
CNA samples ported to C#
```

Find ABI design mistakes before exposing hundreds of classes.

---

## Phase 4 — Broaden XNA API coverage

Add:

```text
SpriteFont
RenderTarget2D
audio
GamePad
Mouse
effects
3D resources
Model
content types
states
advanced SpriteBatch overloads
```

Build parity matrices.

---

## Phase 5 — Serious XNA compatibility tests

Try increasingly complex open-source XNA projects.

Measure:

```text
compile compatibility
source modifications required
runtime behavior
render parity
content compatibility
performance
```

---

## Phase 6 — Packaging

Ship:

```text
NuGet packages
native runtime packages/binaries
samples
documentation
CI release pipeline
```

Make installation simple.

---

## Phase 7 — `cna-js`

Reuse the stable C ABI.

Implement:

```text
TypeScript API
WebAssembly bridge
browser game lifecycle
resource wrappers
SpriteBatch batching
npm packaging
WebGL2/WebGPU validation
```

---

## Phase 8 — Rust and Python

Only after real ABI stability.

---

## Phase 9 — Additional languages

Only where there is demand or a strong strategic use case.

---

# 68. Design invariants that should never be violated

1. **C++ CNA remains canonical.**

2. **The C ABI is the canonical language interoperability boundary.**

3. **The C ABI belongs in the main CNA repository.**

4. **Bindings do not directly depend on arbitrary C++ ABI details.**

5. **C++ exceptions never cross the C ABI.**

6. **Ownership is explicit and testable.**

7. **Strings use an explicit encoding, preferably UTF-8.**

8. **ABI-visible primitive sizes are explicit.**

9. **High-frequency traffic can be batched.**

10. **Math/value types do not need pointless native calls.**

11. **Renderer internals remain native.**

12. **XNA source compatibility is more important than pretending to offer impossible universal binary compatibility.**

13. **C# gets the strongest XNA-fidelity guarantee.**

14. **Other languages preserve XNA concepts but use their own idioms.**

15. **Do not create a binding repository before there is a reason to maintain it.**

---

# 69. Recommended naming

Strong candidates:

```text
Main engine:
openeggbert/cna

C#:
openeggbert/cna-dotnet

JS/TS:
openeggbert/cna-js
npm: @openeggbert/cna

Rust:
openeggbert/cna-rs

Python:
openeggbert/cna-python

Java:
openeggbert/cna-java

Zig:
openeggbert/cna-zig

Go:
openeggbert/cna-go

Swift:
openeggbert/cna-swift
```

Inside .NET:

```text
CNA.Interop
CNA.Framework
CNA.XnaCompat
```

Inside Rust:

```text
cna-sys
cna
```

Names can change later, but the separation of raw interop from high-level API is strongly recommended.

---

# 70. Recommended first real success criterion

The first major success should not be:

> "We generated bindings for 500 classes."

It should be:

> **A normal C# XNA-style game runs through the C++ CNA runtime, draws using an existing CNA renderer, accepts input, loads content, shuts down safely, and works on multiple operating systems.**

For example:

```text
C# Game1
    ↓
CNA.XnaCompat
    ↓
CNA.Interop
    ↓
C API
    ↓
CNA C++
    ↓
OPENGL33 on Linux
```

and the same game:

```text
C# Game1
    ↓
same managed API
    ↓
same C API
    ↓
same CNA C++
    ↓
another supported renderer on Windows
```

Once that works, the architecture is real rather than theoretical.

---

# 71. Recommended long-term success criterion

A mature CNA binding ecosystem could eventually support this statement:

> CNA is a native C++ implementation of an XNA-inspired/XNA-compatible game framework with a stable C interoperability layer and first-class language frontends. Existing XNA 4.0 C# projects can often be recompiled against the .NET compatibility layer, while other languages use idiomatic bindings over the same native engine.

That is much stronger and more precise than claiming:

> "Every XNA game runs automatically."

The latter would be technically unsafe.

---

# 72. What should *not* be promised

Do not promise:

```text
every historical XNA binary runs unchanged
every XNA game works
every custom Content Pipeline extension works
every Windows-specific dependency becomes portable
every historical Xbox Live/GFWL service is reproduced
zero-overhead FFI for every call
every CNA API is immediately available in every language
```

Instead publish compatibility matrices and measured behavior.

---

# 73. Possible compatibility matrix

For .NET:

```text
API                                 Status
------------------------------------------------
Game                                Full
GameTime                            Full
GraphicsDeviceManager               Full
GraphicsDevice                      Partial/Full
SpriteBatch                         Full
Texture2D                           Full
SpriteFont                          Partial
Keyboard                            Full
Mouse                               Full
GamePad                             Partial
BasicEffect                         Partial
Effect                              Partial
Model                               Partial
RenderTarget2D                      Full
XACT                                Planned/Partial
GamerServices                       Replacement/Partial
XNB built-in types                  Partial
custom ContentTypeReader            Case-by-case
```

The exact statuses should be generated from tests, not guessed.

---

# 74. Binding generation should not replace human API design

Automation can generate:

```text
raw FFI declarations
enum mappings
simple value structs
repetitive resource wrappers
documentation tables
ABI parity checks
```

Humans/strong design agents should explicitly design:

```text
Game lifecycle
resource ownership
callback architecture
ContentManager generics
Effect API
collections
async APIs
threading
error mapping
bulk data transfer
language-specific ergonomics
```

A blindly generated wrapper can compile and still be terrible to use or unsafe.

---

# 75. Performance principles for all bindings

The most important rules:

### Keep high-frequency math local

Do not cross FFI for simple vector/matrix calculations.

### Batch repetitive operations

Examples:

```text
SpriteBatch draw commands
vertex uploads
texture data transfers
possibly audio command batches
```

### Cache native lookups

Examples:

```text
effect parameter names
content identifiers
resource IDs
```

### Prefer snapshots

Examples:

```text
keyboard
mouse
gamepad
touch
```

### Avoid per-element marshalling

Move arrays/spans/buffers in bulk.

### Measure before over-optimizing

The first implementation should favor correctness and clear contracts. Optimize proven bottlenecks later.

---

# 76. Threading principles

The C ABI should document which operations must run on:

```text
main thread
graphics thread
audio thread
any thread
```

Language runtimes have different scheduling and GC behavior.

The binding should not assume:

```text
C# finalizer thread may destroy GPU resource safely
JS callback may invoke graphics API from arbitrary async continuation
Python GC destruction occurs on the expected native thread
```

If native destruction must be deferred, provide an explicit CNA release queue.

This must be part of the ABI contract, not an undocumented implementation detail.

---

# 77. Error handling principles by language

The C ABI uses:

```text
CNA_Result
+
error detail retrieval
```

Bindings convert this idiomatically.

C#:

```text
exceptions
```

Rust:

```text
Result<T, CnaError>
```

Python:

```text
Python exceptions
```

Java:

```text
Java exceptions
```

JS/TS:

```text
exceptions / rejected Promise where asynchronous
```

Go:

```text
error
```

Zig:

```text
error unions
```

The same native error model supports language-specific ergonomics.

---

# 78. Asynchronous APIs

The initial binding should avoid inventing async versions of everything.

If CNA has inherently asynchronous functionality later, the C ABI should expose explicit completion mechanisms:

```text
callback
pollable future handle
event handle
completion queue
```

Bindings can map that to:

```text
Task<T>            in C#
Promise<T>         in JS
Future             in Rust
CompletableFuture  in Java
asyncio Future     in Python
```

Do not let language bindings invent incompatible behavior for the same native operation.

---

# 79. ABI and renderer independence

A major design goal is that language bindings do not need a release merely because a new CNA renderer is added.

Good:

```text
new renderer added to CNA C++
    ↓
existing CNA C ABI renderer enum discovery / capabilities
    ↓
binding can use it with minimal or no code changes
```

Avoid hardcoding every renderer everywhere if a capability/discovery API can reduce churn.

For example:

```c
size_t cna_renderer_count(void);

CNA_Result cna_renderer_get_info(
    size_t index,
    CNA_RendererInfo* out_info);
```

This could complement stable known enum identities.

---

# 80. The C ABI could support more than language bindings

Once stable, it can also help:

```text
plugin systems
editor integrations
dynamic modules
test harnesses
foreign-language scripting
FFI-based tooling
runtime embedding
external diagnostics
```

This is another reason to treat it as a core CNA feature rather than a .NET-specific hack.

---

# 81. Strategic risk: maintenance multiplication

Bindings reduce implementation duplication, but they do not reduce maintenance cost to zero.

Every public binding adds:

```text
package releases
CI jobs
platform testing
documentation
examples
language runtime compatibility
bug reports
FFI debugging
version compatibility
```

Therefore the language ecosystem should expand incrementally.

The stable C ABI is the leverage point that keeps the cost manageable.

---

# 82. Strategic risk: API over-commitment

A binding can accidentally freeze unstable experimental CNA APIs.

Therefore consider tiers:

```text
stable C ABI
experimental C ABI extensions
internal/generated interop
```

Not every experimental C++ API needs immediate stable ABI exposure.

A feature should cross into the stable ABI only when its ownership and semantics are mature enough.

---

# 83. Strategic risk: ABI size explosion

Do not mechanically export all CNA C++ methods one-for-one.

A stable ABI should favor:

```text
compact primitives
bulk operations
opaque handles
capability queries
clear lifecycle
```

High-level language wrappers can recreate object-oriented convenience.

This keeps the ABI smaller and more maintainable.

---

# 84. Strategic risk: false "XNA compatibility" marketing

The most credible path is to publish concrete conformance data:

```text
API coverage percentage
tested classes/methods
sample projects passing
known content limitations
known behavioral deviations
renderer-specific differences
```

That is more valuable than broad unverified claims.

---

# 85. Potential automated XNA API parity process

Because CNA already models many XNA namespaces/types, the .NET binding can use an automated parity database.

Possible pipeline:

```text
XNA 4 reference public API
        ↓
normalized symbol database
        ↓
compare with CNA.XnaCompat public API
        ↓
report:
    missing types
    missing methods
    missing overloads
    signature differences
    enum differences
```

Then runtime behavior tests complement signature parity.

This would help keep CNA.NET aligned with the intended XNA 4.0 surface.

---

# 86. Potential use of the existing XNA reference work

Existing XNA 4.0 decompilation/reference analysis can be highly valuable for:

```text
method semantics
edge cases
math behavior
input state behavior
exceptions
property defaults
overload behavior
public API shape
```

The reference material should be treated carefully with respect to licensing and used as a behavioral/reference aid according to the project's legal policy.

The binding itself should expose CNA's implementation, not embed Microsoft's original binaries.

---

# 87. Suggested ABI documentation sections

A future `docs/c-api/` should contain at least:

```text
README.md
ABI_VERSIONING.md
OWNERSHIP.md
ERROR_HANDLING.md
THREADING.md
STRINGS.md
CALLBACKS.md
HANDLES.md
BUFFER_TRANSFER.md
RENDERER_SELECTION.md
BINDING_GUIDE.md
```

Each language binding should be able to rely on these documents instead of re-discovering native rules.

---

# 88. Suggested binding acceptance criteria

A binding should not be called "supported" merely because it compiles.

Suggested minimum:

```text
installation works from the normal package manager
HelloGame runs
texture loading works
input works
clean shutdown works
resource lifetime tests pass
two or more CI platforms pass where appropriate
at least one real nontrivial sample works
ABI mismatch produces a clear error
documentation includes setup and limitations
```

For `CNA.XnaCompat`, add:

```text
XNA API parity tests
at least one existing XNA project recompiles
behavior comparison tests
```

---

# 89. Suggested release sequence

A cautious release path:

```text
C ABI experimental 0.x
    ↓
CNA.NET experimental
    ↓
real project testing
    ↓
ABI cleanup
    ↓
C ABI 1.0
    ↓
CNA.NET stable
    ↓
cna-js experimental
    ↓
Rust/Python
```

Do not declare ABI 1.0 before real bindings have exercised it.

The .NET binding should be used as the first major ABI design validator.

---

# 90. What this plan intentionally replaces

This binding architecture replaces several less attractive ideas:

### Rewriting all XNA games into C++

Not necessary once a C# compatibility layer exists.

### Rewriting all CNA into every target language

Not necessary; use bindings.

### Maintaining renderer implementations per language

Not necessary; bindings share native renderers.

### Creating a separate `cna-c` engine repository

Not recommended; C ABI belongs in CNA.

---

# 91. Recommended project focus

The binding work should be viewed as a strategic extension of CNA, but not as permission to create an unlimited number of side projects.

A good focused roadmap is:

```text
CNA core stability
    ↓
stable interoperability design
    ↓
C ABI
    ↓
CNA.NET
    ↓
real XNA compatibility validation
    ↓
JS/TS
    ↓
selected additional bindings
```

This is more coherent than simultaneously starting unrelated large frameworks.

---

# 92. Final recommendation

The most promising long-term architecture is:

```text
                         Applications
                              │
        ┌─────────────────────┼─────────────────────┐
        ↓                     ↓                     ↓
       C#                   TypeScript             Rust
        │                     │                     │
  CNA.XnaCompat          @openeggbert/cna          cna
        │                     │                     │
  CNA.Framework            JS/WASM                cna-sys
        │                     │                     │
  CNA.Interop                └─────────┬────────────┘
        │                              │
        └──────────────────────┬───────┘
                               ↓
                         CNA stable C ABI
                               ↓
                          CNA C++ core
                               ↓
          ┌────────────────────┼─────────────────────┐
          ↓                    ↓                     ↓
       graphics              audio                content
          │
          ↓
       renderer abstraction
          │
    ┌─────┼──────────────┬───────────────┬──────────────┐
    ↓     ↓              ↓               ↓              ↓
 Vulkan  D3D          OpenGL           WebGPU          ...
```

The most important near-term decisions are:

1. **Keep C++ CNA as the only canonical engine implementation.**
2. **Add the stable C ABI directly to `openeggbert/cna`.**
3. **Use opaque handles, explicit ownership, explicit UTF-8, fixed-width ABI types, explicit errors, and strict ABI versioning.**
4. **Build `openeggbert/cna-dotnet` first.**
5. **Make C# highly XNA 4.0 source-compatible where realistically possible.**
6. **Use .NET interop via the C ABI rather than C++/CLI as the cross-platform foundation.**
7. **Prove the design with real C# games before creating many bindings.**
8. **Build `cna-js`/TypeScript as a strong next candidate, especially through WebAssembly for browsers.**
9. **Add Rust, Python, Java, Zig, Go, Swift, and other bindings incrementally, not simultaneously.**
10. **Do not claim that every XNA game or binary will automatically work; measure and publish compatibility.**

If executed well, this can transform CNA from:

> a C++ XNA-style framework

into:

> **a multi-language native game framework/runtime with a canonical C++ core, a stable C interoperability layer, high-value XNA-compatible .NET support, and idiomatic frontends for additional languages.**

That is one of the most strategically useful ways to expand CNA without duplicating the engine itself.
