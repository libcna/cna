# CNA Language Bindings — Sharp Runtime Integration Addendum

**Companion document to:** `analysis_binding.md`  
**Date:** 2026-08-14  
**Primary repositories:** `openeggbert/cna`, `openeggbert/sharp-runtime`  
**Purpose:** Define the architectural relationship between Sharp Runtime, CNA, the stable CNA C ABI, and all present or future language bindings.

---

# 1. Purpose of this addendum

The main `analysis_binding.md` document defines the recommended strategy for exposing CNA to multiple programming languages:

```text
language-specific API
        ↓
language-specific interop layer
        ↓
stable CNA C ABI
        ↓
canonical CNA C++ implementation
```

This addendum clarifies where **Sharp Runtime** belongs in that architecture.

Sharp Runtime is relevant because CNA already uses it as a C++ support/runtime library in multiple modules. However, Sharp Runtime must **not** become part of the public cross-language ABI contract.

The central rule is:

> **Sharp Runtime is a native C++ dependency and implementation layer. Language bindings should normally not know that Sharp Runtime exists.**

The stable CNA C ABI must isolate C#, JavaScript, Rust, Python, Java, Zig, Go, Swift, and any future language binding from Sharp Runtime's internal C++ types and implementation details.

---

# 2. What Sharp Runtime is

Sharp Runtime is a C++23 implementation of a practical subset of the .NET `System.*` libraries.

Its role is to provide familiar .NET-style APIs in native C++, especially for CNA and other native ports.

Conceptually:

```text
.NET / C#
System.String
System.IO.Stream
System.Collections.Generic.List<T>
System.Threading.Tasks.Task
...
```

have native C++ counterparts or approximations in Sharp Runtime.

Sharp Runtime is **not** intended to be:

```text
a CLR
a .NET VM
a JIT compiler
a garbage collector
a complete replacement for .NET
a managed-language runtime
```

It is a C++ library that provides useful .NET-like APIs and behavior.

At the current project baseline, Sharp Runtime is physically modularized into many independently selectable CMake components, with areas covering concepts such as:

```text
core types
strings
spans
dates and times
exceptions
delegates
collections
immutable collections
concurrent collections
text
regular expressions
globalization
JSON
XML
streams
files
compression
hashing
networking
HTTP
WebSockets
threads
tasks
channels
timers
synchronization
numerics
selected cryptographic primitives
```

This makes Sharp Runtime useful internally to CNA because CNA is inspired by and compatible with APIs originally designed for the .NET/XNA ecosystem.

---

# 3. The most important architectural distinction

There are two very different worlds:

## Native C++ world

```text
CNA C++
    ↓
Sharp Runtime
```

Sharp Runtime can be used freely where CNA's native implementation benefits from it.

## Foreign-language world

```text
C#
JavaScript
Rust
Python
Java
Zig
Go
Swift
...
    ↓
CNA language binding
    ↓
CNA C ABI
```

These languages should **not** bind to Sharp Runtime directly merely because CNA uses Sharp Runtime internally.

The two worlds meet at:

```text
CNA stable C ABI
```

That is the isolation boundary.

---

# 4. Canonical architecture

The recommended full architecture is:

```text
                         Applications
                              │
       ┌──────────────────────┼────────────────────────┐
       ↓                      ↓                        ↓
      C#                   TypeScript                 Rust
       │                      │                        │
 CNA.XnaCompat          @openeggbert/cna              cna
       │                      │                        │
 CNA.Framework             JS/WASM                  cna-sys
       │                      │                        │
 CNA.Interop                 └────────────┬───────────┘
       │                                 │
       └─────────────────────────┬───────┘
                                 ↓
                           CNA stable C ABI
                                 ↓
                            CNA C++ core
                                 │
                 ┌───────────────┼────────────────┐
                 ↓               ↓                ↓
          Sharp Runtime       CNA native       renderers
            components        subsystems
                 │               │
                 └───────┬───────┘
                         ↓
                    native platform
```

Sharp Runtime is below the C ABI boundary.

It is an implementation dependency of the native side.

---

# 5. Sharp Runtime is not CNA.NET

This distinction must remain explicit.

`CNA.NET` is the C#/.NET language frontend for CNA.

Sharp Runtime is a native C++ library.

They are not the same thing.

Do not design:

```text
C# CNA application
        ↓
C# wrappers around Sharp Runtime
        ↓
Sharp Runtime
        ↓
CNA
```

The correct architecture is:

```text
C# CNA application
        ↓
real .NET runtime and BCL
        ↓
CNA.NET
        ↓
CNA C ABI
        ↓
CNA C++
        ↓
Sharp Runtime where CNA internally needs it
```

C# already has the actual .NET Base Class Library.

Therefore C# code should naturally use:

```csharp
System.String
System.IO.Stream
System.Collections.Generic.List<T>
System.Threading.Tasks.Task
System.DateTime
System.Uri
System.Net.Http.HttpClient
```

It should not unnecessarily wrap or emulate these through Sharp Runtime.

---

# 6. A useful symmetry

The architecture creates a clean conceptual symmetry.

In native C++ CNA code:

```text
CNA C++
    ↓
Sharp Runtime
    ↓
.NET-like APIs implemented in C++
```

In C# CNA code:

```text
CNA.NET
    ↓
real .NET runtime
    ↓
real System.* APIs
```

Therefore both worlds can use familiar concepts without pretending to share the same runtime objects.

Example:

Native CNA implementation:

```cpp
System::String path;
```

Managed C# application:

```csharp
string path;
```

The ABI transfers the value:

```text
C# string
    ↓
UTF-8 bytes
    ↓
CNA C ABI
    ↓
C++ / Sharp Runtime string representation
```

The objects themselves do not cross the boundary.

---

# 7. Sharp Runtime types must not leak into the stable CNA C ABI

This is one of the strongest rules in the entire binding architecture.

Do not expose:

```c
SharpRuntime_String*
SharpRuntime_Object*
SharpRuntime_Stream*
SharpRuntime_Task*
SharpRuntime_List*
SharpRuntime_Dictionary*
SharpRuntime_Exception*
```

as public CNA ABI types.

Do not expose C++ names such as:

```cpp
System::String
System::Collections::Generic::List<T>
System::IO::Stream
System::Threading::Tasks::Task
```

through `extern "C"` signatures.

Instead normalize them into stable C ABI constructs.

Examples:

```text
Sharp Runtime string
        ↓
UTF-8 pointer + length

Sharp Runtime List<T>
        ↓
pointer + count
or
count/copy API

Sharp Runtime Stream
        ↓
CNA stream handle

Sharp Runtime Task
        ↓
CNA async-operation handle / callback

Sharp Runtime exception
        ↓
CNA_Result + error information
```

This keeps the ABI stable even if CNA changes its internal implementation.

---

# 8. Why this isolation is important

Suppose CNA internally changes from:

```cpp
System::Collections::Generic::List<DisplayMode>
```

to:

```cpp
std::vector<DisplayMode>
```

or vice versa.

If the C ABI is neutral:

```text
CNA display-mode enumeration API
```

then:

```text
C#
Rust
Java
Python
JS
Zig
...
```

do not need to change.

Only the implementation inside `openeggbert/cna` changes.

This is exactly what a stable ABI should accomplish.

---

# 9. Sharp Runtime should remain replaceable internally

Even if Sharp Runtime is a major CNA dependency, the language binding architecture should avoid making it impossible to replace individual internal components later.

For example, CNA may eventually decide that a particular subsystem is better implemented using:

```text
std::filesystem
std::chrono
std::vector
another networking library
platform-native APIs
```

instead of the corresponding Sharp Runtime type.

Bindings should not care.

The dependency direction should remain:

```text
bindings
    ↓
C ABI
    ↓
CNA implementation
    ↓
optional/internal Sharp Runtime usage
```

not:

```text
bindings
    ↓
Sharp Runtime ABI
    ↓
CNA
```

---

# 10. Recommended repository boundaries

## `openeggbert/sharp-runtime`

Owns:

```text
C++23 .NET-like runtime/library functionality
Sharp Runtime modules/components
Sharp Runtime tests
Sharp Runtime documentation
Sharp Runtime internal/public C++ API
```

## `openeggbert/cna`

Owns:

```text
CNA C++ implementation
CNA renderer implementations
CNA public C++ API
CNA stable C ABI
CNA adaptation of Sharp Runtime concepts into CNA ABI concepts
CNA ABI tests
```

## `openeggbert/cna-dotnet`

Owns:

```text
C# interop declarations
C# SafeHandle/resource wrappers
CNA.Framework
CNA.XnaCompat
NuGet packaging
C# tests
C# samples
```

## Other binding repositories

Own only language-specific presentation and interop.

They do not own Sharp Runtime abstractions.

---

# 11. Dependency graph

A good dependency graph is:

```text
openeggbert/sharp-runtime
            ↑
            │ native dependency
            │
      openeggbert/cna
            │
            │ exposes
            ↓
       CNA C ABI
            │
     ┌──────┼───────────┬───────────┬───────────┐
     ↓      ↓           ↓           ↓           ↓
    C#      JS         Rust       Python       Java
```

Sharp Runtime never depends on language bindings.

Language bindings normally do not depend directly on Sharp Runtime.

CNA is the bridge.

---

# 12. Public CNA C++ API versus public C ABI

CNA's C++ API may legitimately expose Sharp Runtime types if that is part of CNA's C++ design.

For example, it may be natural for a C++ CNA API to use:

```cpp
System::String
System::IO::Stream
```

where the project deliberately wants .NET-like semantics.

That is separate from the C ABI.

The stable C ABI should convert those concepts to language-neutral forms.

Therefore CNA may have:

```text
public C++ API
    possibly Sharp Runtime-aware

public C ABI
    Sharp Runtime-neutral
```

This is acceptable and desirable.

---

# 13. Strings

Sharp Runtime may use its own `.NET-like` string abstraction.

Bindings must not depend on its storage layout.

Recommended ABI representation:

```c
typedef struct CNA_StringView {
    const char* data;
    size_t byte_length;
} CNA_StringView;
```

or explicit function arguments:

```c
CNA_Result cna_content_load_texture2d(
    CNA_Handle content_manager,
    const char* utf8_name,
    size_t utf8_name_length,
    CNA_Handle* out_texture);
```

For returned strings, use an ownership-safe pattern.

Possible designs:

### Caller-owned buffer

```c
size_t cna_object_get_name_size(
    CNA_Handle object);

CNA_Result cna_object_copy_name(
    CNA_Handle object,
    char* buffer,
    size_t capacity,
    size_t* written);
```

### CNA-owned temporary view

Only if lifetime rules are extremely clear.

### Allocator API

Possible but adds allocator complexity and should not be the default unless justified.

UTF-8 should be the standard boundary representation.

---

# 14. Collections

Sharp Runtime has extensive collection support.

None of those collection implementation types should cross the ABI.

Example native implementation:

```cpp
System::Collections::Generic::List<DisplayMode>
```

Possible C ABI:

```c
size_t cna_graphics_adapter_get_display_mode_count(
    CNA_Handle adapter);

CNA_Result cna_graphics_adapter_copy_display_modes(
    CNA_Handle adapter,
    CNA_DisplayMode* destination,
    size_t capacity,
    size_t* written);
```

C# wrapper:

```csharp
IReadOnlyList<DisplayMode>
```

Rust wrapper:

```rust
Vec<DisplayMode>
```

Python wrapper:

```python
list[DisplayMode]
```

Java wrapper:

```java
List<DisplayMode>
```

The language uses its own native collection.

---

# 15. Dictionaries/maps

If CNA internally uses something like a Sharp Runtime dictionary:

```cpp
Dictionary<String, EffectParameter>
```

do not expose a dictionary object through ABI.

Instead provide explicit operations:

```text
get count
enumerate keys
find by UTF-8 name
resolve stable parameter handle
```

For performance-sensitive name lookup:

```text
string name
    ↓
resolve once
    ↓
native handle/id
```

Then repeated operations use the handle.

This avoids repeated string conversion and avoids exposing internal collection semantics.

---

# 16. Exceptions

Sharp Runtime provides .NET-like exception types in C++.

These are useful inside native code.

They must not cross the C ABI.

Correct flow:

```text
Sharp Runtime / C++ exception
            ↓
catch at C ABI boundary
            ↓
CNA_Result
+
structured/native error information
            ↓
binding-specific error
```

Examples:

C#:

```text
CNA_Result
    ↓
CnaException
```

Rust:

```text
CNA_Result
    ↓
Result<T, CnaError>
```

Java:

```text
CNA_Result
    ↓
CnaException
```

Python:

```text
CNA_Result
    ↓
Python exception
```

JS:

```text
CNA_Result
    ↓
Error / rejected Promise
```

The C ABI boundary is the mandatory exception firewall.

---

# 17. Error information

The C ABI should offer structured error retrieval independent of Sharp Runtime exception classes.

Possible design:

```c
typedef enum CNA_ErrorCategory {
    CNA_ERROR_CATEGORY_NONE,
    CNA_ERROR_CATEGORY_ARGUMENT,
    CNA_ERROR_CATEGORY_IO,
    CNA_ERROR_CATEGORY_GRAPHICS,
    CNA_ERROR_CATEGORY_CONTENT,
    CNA_ERROR_CATEGORY_AUDIO,
    CNA_ERROR_CATEGORY_PLATFORM,
    CNA_ERROR_CATEGORY_INTERNAL
} CNA_ErrorCategory;
```

Possible last-error information:

```c
typedef struct CNA_ErrorInfo {
    int32_t code;
    CNA_ErrorCategory category;
    const char* message_utf8;
    size_t message_length;
} CNA_ErrorInfo;
```

Actual design may use thread-local storage, caller buffers, or explicit error objects.

The important point:

> Binding code must not need to understand Sharp Runtime exception class hierarchy.

---

# 18. Streams

Sharp Runtime contains stream abstractions.

CNA may use them internally.

Bindings should not receive a `SharpRuntime Stream*`.

There are several possible public models.

## Model A — CNA-native stream handle

```text
language stream wrapper
    ↓
CNA stream handle
    ↓
CNA/Sharp Runtime native stream
```

C ABI:

```c
CNA_Result cna_stream_read(
    CNA_Handle stream,
    void* buffer,
    size_t buffer_size,
    size_t* bytes_read);
```

## Model B — callbacks supplied by the language

Useful when native CNA needs to read from a language-owned stream.

Example callback table:

```c
typedef struct CNA_StreamCallbacks {
    size_t (*read)(
        void* context,
        void* destination,
        size_t requested);

    int64_t (*seek)(
        void* context,
        int64_t offset,
        CNA_SeekOrigin origin);

    int64_t (*length)(
        void* context);

    void (*close)(
        void* context);
} CNA_StreamCallbacks;
```

Then CNA adapts this to whatever internal stream abstraction it uses.

This is more flexible than exposing Sharp Runtime directly.

---

# 19. Files and paths

Sharp Runtime may provide:

```text
File
Directory
Path
FileStream
```

CNA bindings should generally use one of two approaches.

### High-level CNA resource APIs

Example:

```text
Content.Load<Texture2D>("player")
```

The language does not manipulate Sharp Runtime paths at all.

### Explicit CNA path/string APIs

The boundary receives UTF-8 path values.

Do not transfer internal path objects.

Each binding can use its own language-native path types where convenient:

```text
C# System.IO / string
Rust Path / PathBuf
Java Path
Python pathlib.Path
JS string/URL
```

and normalize to the CNA ABI.

---

# 20. Date/time types

If CNA exposes time-related values that internally use Sharp Runtime, prefer POD numeric forms through ABI.

Examples:

```text
nanoseconds
microseconds
ticks
seconds as double
fixed-width integer timestamps
explicit epoch
```

For `GameTime`, a language-neutral struct is appropriate:

```c
typedef struct CNA_GameTime {
    int64_t total_time_ticks;
    int64_t elapsed_time_ticks;
    uint8_t is_running_slowly;
} CNA_GameTime;
```

The exact representation should match CNA/XNA semantic requirements.

Bindings reconstruct language-native objects.

---

# 21. Tasks and asynchronous operations

Sharp Runtime includes task/async abstractions.

Do not expose Sharp Runtime `Task` objects through the C ABI.

A neutral native async contract is needed.

Possible pattern:

```text
CNA async operation handle
+
poll
+
wait where supported
+
cancel
+
completion callback
+
result retrieval
```

Example:

```c
CNA_Result cna_async_get_status(
    CNA_Handle operation,
    CNA_AsyncStatus* out_status);

CNA_Result cna_async_cancel(
    CNA_Handle operation);
```

Bindings map this to:

C#:

```csharp
Task<T>
```

JavaScript:

```typescript
Promise<T>
```

Rust:

```rust
Future
```

Java:

```java
CompletableFuture<T>
```

Python:

```python
asyncio.Future
```

The C ABI defines semantics.

Sharp Runtime is free to implement those semantics internally.

---

# 22. Threads and synchronization

Sharp Runtime contains thread and synchronization concepts.

Bindings must not assume that foreign language threads are interchangeable with CNA/Sharp Runtime threads.

The C ABI must document:

```text
which functions are thread-safe
which functions require the main thread
which functions require the graphics thread
which callbacks occur on which thread
which operations may block
which operations may be called concurrently
```

Do not expose raw mutexes, events, threads, or task schedulers from Sharp Runtime as cross-language objects unless a CNA-specific abstraction is deliberately designed.

---

# 23. Delegates and callbacks

Sharp Runtime may provide delegate-like C++ abstractions.

The C ABI should expose plain C function pointers and opaque context pointers.

For example:

```c
typedef void (*CNA_UpdateCallback)(
    void* context,
    const CNA_GameTime* game_time);

typedef void (*CNA_DrawCallback)(
    void* context,
    const CNA_GameTime* game_time);
```

C++ CNA may internally adapt them to:

```text
Sharp Runtime delegate
std::function
virtual method
another callback abstraction
```

The ABI does not care.

This keeps C#, Rust, Java, JS/WASM, etc. independent of the internal delegate implementation.

---

# 24. Object model and `System::Object`-like types

Sharp Runtime may have .NET-like object-model concepts.

Do not expose them as universal foreign-language objects.

The C ABI should use:

```text
typed opaque handles
explicit type APIs
explicit cast/query APIs where necessary
```

Example:

```c
typedef uint64_t CNA_Texture2DHandle;
typedef uint64_t CNA_GraphicsDeviceHandle;
```

or a generic handle with runtime type validation.

Avoid:

```text
one universal SharpRuntime::Object pointer
```

as the public interoperability model.

That would couple every binding to Sharp Runtime's object semantics and lifetime rules.

---

# 25. Reference equality and value equality

If CNA behavior depends on .NET-like semantics such as:

```text
Equals
GetHashCode
reference equality
value equality
```

the C ABI must expose the relevant CNA behavior explicitly where needed.

Do not expect bindings to infer Sharp Runtime semantics from raw handles.

For resource objects, handle identity can often represent object identity.

For value types, bindings should implement compatible equality locally.

---

# 26. Numerics

Sharp Runtime may supply .NET-like numeric types and aliases.

The ABI should use stable primitives or explicit structures.

Good ABI examples:

```text
int32_t
uint32_t
int64_t
uint64_t
float
double
```

For special large or platform-dependent types:

```text
Int128
UInt128
Decimal
BigInteger-like values
```

do not expose compiler-specific native layout.

Use:

```text
byte arrays
fixed explicit word structs
decimal text
dedicated CNA ABI struct
```

depending on semantics and performance needs.

This is especially important because Sharp Runtime has platform/toolchain-specific boundaries for some 128-bit types.

---

# 27. JSON and XML

Sharp Runtime provides JSON/XML functionality.

That does not mean binding APIs should expose Sharp Runtime JSON nodes or XML nodes.

Possible CNA binding strategies:

```text
pass UTF-8 serialized JSON/XML
```

or:

```text
CNA-specific structured objects
```

or:

```text
language parses data itself
```

depending on the subsystem.

For example, C# should usually use:

```text
System.Text.Json
```

for application-owned JSON.

Java should use a Java JSON library.

JS already has native JSON support.

Sharp Runtime remains relevant only to native CNA code that needs JSON/XML.

---

# 28. Networking

Sharp Runtime may supply:

```text
sockets
HTTP
WebSockets
network information
```

CNA may use some of this internally.

Language bindings should expose CNA networking concepts, not Sharp Runtime networking objects.

For example:

```text
NetworkSession
AvailableNetworkSession
CNA-specific multiplayer API
```

may map through the C ABI.

A C# application can still independently use:

```csharp
HttpClient
Socket
ClientWebSocket
```

from real .NET.

There is no reason to route general application networking through Sharp Runtime merely because CNA uses Sharp Runtime internally.

---

# 29. Logging and diagnostics

If Sharp Runtime supplies internal formatting or diagnostics, language bindings should receive CNA-level logging callbacks or messages.

Possible C ABI:

```c
typedef void (*CNA_LogCallback)(
    void* context,
    CNA_LogLevel level,
    const char* message_utf8,
    size_t message_length);
```

Native implementation:

```text
CNA/Sharp Runtime logging
        ↓
normalized CNA log event
        ↓
C ABI callback
        ↓
language logger
```

C# can then adapt to:

```text
ILogger
Console
custom logging
```

Python to `logging`.

Java to its logging stack.

JS to console/custom telemetry.

---

# 30. Environment and platform information

Sharp Runtime may expose environment helpers internally.

CNA bindings should expose only CNA-relevant platform/capability APIs.

For example:

```text
current CNA platform
renderer support
display information
storage locations
input capabilities
```

Do not simply mirror all Sharp Runtime environment APIs into every binding.

The bindings already have their language/runtime equivalents.

---

# 31. Why wrapping Sharp Runtime directly would be a mistake

Suppose `cna-dotnet` attempted to wrap Sharp Runtime types.

Then the architecture could become:

```text
C# System.String
        ↓ convert
SharpRuntime::System::String
        ↓
CNA
```

This creates:

```text
duplicate .NET concepts
extra allocations
extra marshalling
more lifetime complexity
larger ABI
more version coupling
confusing documentation
```

The C# developer would have two versions of familiar types:

```text
System.String
SharpRuntime.String wrapper
```

That would be unnecessary and harmful.

Use real .NET in C#.

Use Sharp Runtime only where C++ CNA needs .NET-like behavior.

---

# 32. The same rule applies to Java

Java application:

```text
java.lang.String
java.util.List
java.time.*
java.io.*
java.net.*
```

should use the real Java standard library.

CNA Java binding:

```text
Java
    ↓
cna-java
    ↓
CNA C ABI
    ↓
CNA C++
    ↓
Sharp Runtime internally
```

Do not expose Sharp Runtime's `.NET-like` general-purpose library API to Java merely because it exists.

---

# 33. The same rule applies to JavaScript

JS/TS application uses:

```text
string
Array
Map
Set
Promise
fetch
WebSocket
TypedArray
```

The CNA JS binding uses WebAssembly/native exports for CNA-specific functionality.

Sharp Runtime remains inside the C++/WASM module.

A browser user should not even need to know Sharp Runtime exists.

---

# 34. The same rule applies to Rust

Rust uses:

```text
String
Vec<T>
HashMap
Result
Future
std::fs
std::net
```

The high-level Rust binding should use those idioms.

Raw `cna-sys` binds to CNA C ABI.

It does not bind directly to Sharp Runtime's C++ API.

---

# 35. The same rule applies to Python

Python uses:

```text
str
list
dict
bytes
pathlib
asyncio
exceptions
```

CNA Python wrappers should present normal Python values.

Do not invent:

```python
SharpRuntimeString
SharpRuntimeList
SharpRuntimeTask
```

for ordinary CNA operations.

---

# 36. The same rule applies to Zig, Go, and Swift

Each language should use its own:

```text
string type
collections
error model
async model
path/file types
```

The CNA C ABI converts values to stable language-neutral forms.

Sharp Runtime remains on the native side.

---

# 37. C ABI ownership versus Sharp Runtime ownership

Sharp Runtime may use:

```text
RAII
smart pointers
custom object ownership
reference-counted structures
value semantics
```

The ABI must not require foreign languages to understand these internal rules.

Instead define explicit CNA ownership categories.

Example:

```text
BORROWED HANDLE
    caller must not release

OWNED HANDLE
    caller must release

SHARED HANDLE
    retain/release semantics

VALUE
    copied by value

VIEW
    valid only until documented boundary
```

These categories must be documented in CNA C ABI terms.

They should not say:

> "This is owned because Sharp Runtime uses shared_ptr internally."

That implementation detail may change.

---

# 38. Handle implementation may still use Sharp Runtime

Internally, a CNA handle table could use:

```text
Sharp Runtime collections
std::vector
std::unordered_map
custom table
```

This is completely fine.

The public handle format and behavior must remain stable.

Example:

```text
64-bit CNA handle
    ↓
slot + generation
    ↓
internal object ownership
```

Bindings depend only on:

```text
CNA handle contract
```

not how the native handle table is implemented.

---

# 39. Native allocation

Bindings should not normally allocate Sharp Runtime objects themselves.

Creation should happen through CNA functions:

```c
CNA_Result cna_texture2d_create(...);
```

CNA decides internally whether this creates:

```text
C++ object
Sharp Runtime-aware object
shared_ptr
unique_ptr
custom resource
```

Likewise destruction happens through CNA:

```c
cna_texture2d_release(handle);
```

This prevents allocator/CRT/runtime mismatches across boundaries.

---

# 40. Memory buffers

For bulk binary data:

```text
texture pixels
vertex data
index data
audio samples
file bytes
shader bytes
```

prefer explicit buffers:

```c
CNA_Result cna_texture2d_set_data(
    CNA_Handle texture,
    const void* data,
    size_t byte_length);
```

Do not pass:

```text
Sharp Runtime arrays
Sharp Runtime spans
std::vector
```

across the ABI.

Bindings map their native buffer types:

C#:

```text
Span<T>
ReadOnlySpan<T>
Memory<T>
byte[]
```

Rust:

```text
&[T]
Vec<T>
```

JS:

```text
TypedArray
ArrayBuffer
```

Python:

```text
bytes
bytearray
memoryview
```

to pointer/length or copying APIs.

---

# 41. Zero-copy should be explicit, not accidental

Sharp Runtime may use span-like constructs.

The C ABI can support zero-copy access where safe, but it must define:

```text
who owns memory
how long pointer remains valid
whether memory may move
whether caller may mutate it
thread restrictions
alignment
element layout
```

Do not rely on an internal Sharp Runtime span lifetime accidentally remaining valid.

If zero-copy is not clearly safe, use an explicit copy API.

Correctness is more important than micro-optimization at the initial binding stage.

---

# 42. `GameTime`

CNA's XNA-style `GameTime` is a good example of a type that may internally depend on Sharp Runtime time abstractions but should cross the ABI as a value struct.

Conceptual C ABI:

```c
typedef struct CNA_GameTime {
    int64_t total_game_time_ticks;
    int64_t elapsed_game_time_ticks;
    uint8_t is_running_slowly;
} CNA_GameTime;
```

C# reconstructs:

```csharp
GameTime
```

Rust:

```rust
GameTime
```

JS:

```typescript
GameTime
```

No binding needs access to Sharp Runtime's internal time class.

---

# 43. Math types

The same principle applies to:

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
Plane
Ray
```

If Sharp Runtime contributes numeric helpers internally, that is irrelevant to the binding.

The language wrapper should normally implement small value operations locally.

The C ABI only transfers them when calling native CNA operations.

---

# 44. Content system

CNA may use Sharp Runtime internally for:

```text
paths
streams
collections
exceptions
serialization
file IO
```

The public binding should still expose CNA/XNA concepts:

```text
ContentManager
Content.Load<T>()
Content.RootDirectory
```

C# example:

```csharp
texture =
    Content.Load<Texture2D>("player");
```

Flow:

```text
C# string
    ↓
CNA.NET
    ↓
UTF-8 CNA C ABI
    ↓
CNA C++
    ↓
Sharp Runtime path/stream/file APIs as needed
    ↓
native content loader
```

The application never handles Sharp Runtime directly.

---

# 45. Graphics system

Sharp Runtime should be almost invisible at the language-facing graphics API.

Example:

```text
C# Texture2D
Rust Texture2D
JS Texture2D
Python Texture2D
```

all map to:

```text
CNA_Texture2D handle
```

which maps to:

```text
C++ CNA Texture2D
```

Sharp Runtime may support utility types internally, but the graphics ABI is defined by CNA.

---

# 46. Renderer system

Renderer implementation remains entirely native.

Bindings must not bind to Sharp Runtime renderer-related internals.

Flow:

```text
language renderer selection
        ↓
CNA renderer identity
        ↓
C ABI
        ↓
CNA renderer factory
        ↓
selected native renderer
```

Sharp Runtime's presence or absence in renderer helper code should have no binding-level effect.

---

# 47. Audio/media system

If CNA audio/media internally uses Sharp Runtime for:

```text
streams
collections
timers
threads
exceptions
```

bindings still expose:

```text
SoundEffect
SoundEffectInstance
Song
MediaPlayer
AudioEngine
or CNA-specific equivalents
```

as CNA resources/values.

Do not expose underlying Sharp Runtime streams, tasks, or thread primitives.

---

# 48. Networking/gamer-services system

The same rule applies to CNA networking.

CNA's public language binding exposes CNA/XNA-compatible networking abstractions.

Sharp Runtime networking components may be used internally.

For example:

```text
C# NetworkSession
    ↓
CNA.NET
    ↓
C ABI session handle
    ↓
CNA C++ network subsystem
    ↓
Sharp Runtime sockets/network helpers
```

The C# application can independently use real .NET networking APIs for its own unrelated networking.

---

# 49. Serialization

If CNA needs to serialize native state using Sharp Runtime JSON/XML APIs, the serialized format should be considered separately from the in-memory Sharp Runtime object model.

Foreign languages can interoperate through:

```text
defined file format
JSON text
XML text
binary format
CNA-specific serialization API
```

not through native Sharp Runtime DOM objects.

---

# 50. ABI generation and Sharp Runtime

If CNA creates machine-readable ABI metadata, Sharp Runtime types appearing in public C++ signatures must be normalized before generation.

Example source concept:

```cpp
System::String GetTitle() const;
```

Generated C ABI should become something like:

```c
size_t cna_window_get_title_size(
    CNA_Handle window);

CNA_Result cna_window_copy_title(
    CNA_Handle window,
    char* utf8_buffer,
    size_t capacity,
    size_t* written);
```

The generator should have explicit mappings:

```text
Sharp Runtime String
    → UTF-8 ABI string convention

Sharp Runtime collection
    → count/copy or pointer/count

Sharp Runtime exception
    → CNA_Result/error

Sharp Runtime Task
    → CNA async abstraction

Sharp Runtime delegate
    → callback function pointer + context
```

Do not emit C ABI names copied directly from Sharp Runtime.

---

# 51. Automated API mapping layer

A future binding generator may benefit from a normalization model.

Conceptually:

```text
CNA C++ public API
        ↓
semantic normalization
        ↓
language-neutral CNA ABI model
        ↓
        ├── C headers
        ├── C++ ABI adapters
        ├── C# interop
        ├── Rust sys bindings
        ├── Java native declarations
        └── JS/WASM exports
```

Sharp Runtime mappings happen in the normalization stage.

This ensures the ABI describes **CNA concepts**, not implementation-library concepts.

---

# 52. Sharp Runtime API changes should not automatically break bindings

Suppose Sharp Runtime changes:

```text
System::String internal representation
collection implementation
exception hierarchy detail
task scheduler implementation
stream implementation
```

The correct expected effect is:

```text
possibly rebuild CNA
possibly update CNA C++ integration
```

but:

```text
no C ABI break
no CNA.NET public API break
no Rust binding break
no JS binding break
```

unless CNA's own semantics changed.

That is the desired encapsulation.

---

# 53. Sharp Runtime version compatibility

CNA should manage compatibility with Sharp Runtime inside its native build.

Language bindings should depend on:

```text
CNA ABI version
```

not:

```text
Sharp Runtime version
```

For example, `cna-dotnet` should specify:

```text
requires CNA ABI >= 1.x and < 2.0
```

not:

```text
requires Sharp Runtime version N
```

A language package user should not need to reason about Sharp Runtime versioning.

---

# 54. Packaging implications

Native CNA distribution may statically or dynamically include/link Sharp Runtime depending on build policy.

This is a native packaging decision.

For a C# NuGet consumer, the ideal remains:

```bash
dotnet add package CNA.Framework
```

The package includes or obtains appropriate CNA native binaries.

The user should not separately install Sharp Runtime unless the native CNA packaging model explicitly requires it.

Likewise JS/WASM:

```text
@openeggbert/cna
```

should package a WASM/native unit in which Sharp Runtime dependencies are already resolved.

Rust/Python/Java bindings should follow the same principle.

---

# 55. Static linking versus shared Sharp Runtime

Two native deployment options may exist:

## Sharp Runtime linked into CNA/native binaries

Advantages:

```text
simpler consumer deployment
fewer runtime library files
version coupling controlled by CNA release
```

## Sharp Runtime shipped as a separate shared library

Advantages may include:

```text
reuse
smaller duplicate native binaries
independent updates
```

but it increases deployment/version complexity.

This decision should be made according to CNA's native packaging goals.

It should **not** change the public language binding API.

---

# 56. ABI tests must guard against Sharp Runtime leakage

Add checks that fail if stable C ABI headers expose forbidden C++/Sharp Runtime constructs.

For example, a validator can reject references to:

```text
namespace System
SharpRuntime
std::
template syntax
C++ references
C++ classes
C++ exceptions
```

inside public C ABI headers.

Possible automated rule:

```text
modules/c-api/include/**
must compile as C11/C17
```

with a real C compiler.

This is an extremely useful regression gate.

---

# 57. Pure C compile test

At minimum:

```c
#include <CNA/C/cna.h>

int main(void)
{
    uint32_t abi = cna_get_abi_version();
    return abi == 0;
}
```

must compile using a C compiler, not C++.

This catches accidental leakage such as:

```text
std::string
namespace
class
template
bool layout assumptions
reference syntax
```

The test should be part of CI.

---

# 58. Linkage tests

In addition to C header compilation, test that a pure C executable can:

```text
link CNA C ABI
initialize runtime
create/destroy a simple object
query version
cleanly shut down
```

This proves Sharp Runtime/C++ details are successfully hidden behind the exported ABI.

---

# 59. ABI symbol inspection

CI can inspect exported symbols.

Expected:

```text
cna_*
```

Avoid unintentionally exporting large portions of:

```text
Sharp Runtime C++ symbols
CNA internal C++ symbols
STL implementation symbols
```

depending on platform/build strategy.

Symbol visibility should be intentionally controlled.

---

# 60. ABI sanitizer tests

Useful testing areas:

```text
ASan
UBSan
TSan where feasible
Valgrind-like tools
Windows diagnostics
handle generation tests
double-free tests
stale-handle tests
buffer bounds
callback lifetime
threading
```

These are especially important because Sharp Runtime and CNA may have complex native ownership, while the binding introduces another lifetime system on top.

---

# 61. .NET lifetime versus native lifetime

C# uses GC.

CNA and Sharp Runtime use native C++ ownership.

These models must meet through explicit handles.

Do not assume:

```text
managed GC lifetime
==
native C++ lifetime
```

Example:

```csharp
Texture2D texture = new(...);
```

owns:

```text
managed wrapper
+
native handle
```

`Dispose()` releases the native resource.

The native object may itself hold Sharp Runtime/C++ resources.

C# does not manage them directly.

This separation is healthy.

---

# 62. Java GC versus native lifetime

Java has the same conceptual problem.

Use:

```text
explicit close/dispose where appropriate
Cleaner only as fallback
native handle
```

Do not allow Java's GC to directly reason about Sharp Runtime ownership.

---

# 63. JavaScript GC versus native lifetime

JS/WASM wrappers should support explicit `dispose()`.

Finalization may be a backup.

GPU/audio/native resources should not depend solely on unpredictable JS garbage-collection timing.

Again:

```text
JS wrapper lifetime
        ↓
CNA handle lifetime
        ↓
C++/Sharp Runtime resource lifetime
```

with explicit transitions.

---

# 64. Rust lifetime

Rust is particularly well suited to the handle model.

A high-level wrapper:

```rust
pub struct Texture2D {
    handle: TextureHandle,
}
```

can implement:

```rust
impl Drop for Texture2D {
    fn drop(&mut self) {
        unsafe {
            cna_texture2d_release(
                self.handle.raw());
        }
    }
}
```

Rust still does not need to understand Sharp Runtime ownership.

It only follows the CNA ABI ownership contract.

---

# 65. Python lifetime

Python wrapper objects can use deterministic context managers:

```python
with Texture2D(device, 512, 512) as texture:
    ...
```

plus explicit:

```python
texture.close()
```

and a finalizer as fallback.

The native C++/Sharp Runtime resource is controlled through CNA handles.

---

# 66. Shared resources

Some CNA resources may be shared internally.

The ABI must define whether language wrappers:

```text
own
borrow
retain
clone
reference
```

the resource.

If internal Sharp Runtime/shared_ptr semantics are used, convert them into explicit ABI retain/release semantics rather than leaking the smart pointer itself.

Possible:

```c
CNA_Result cna_object_retain(
    CNA_Handle handle);

CNA_Result cna_object_release(
    CNA_Handle handle);
```

only where shared ownership is truly required.

Avoid universal reference counting if ownership can remain simpler.

---

# 67. Callbacks and managed object rooting

C# callback context objects must remain rooted while native CNA holds callback pointers.

This is a binding responsibility.

Sharp Runtime callback/delegate internals are irrelevant.

The contract should say:

```text
native CNA stores C function pointer + context
binding guarantees context remains valid
native unregisters callback before context is freed
```

The same pattern applies to:

```text
Java JNI/global references
JS/WASM callback tables
Python callback references
Rust boxed contexts
```

---

# 68. Re-entrancy

If Sharp Runtime or CNA internally dispatches callbacks, bindings must know whether callbacks may re-enter CNA.

The C ABI should explicitly document re-entrancy rules.

Examples:

```text
Update callback may call graphics/input/content functions
Draw callback may call graphics functions
shutdown callback may not create new resources
logging callback must not recursively log
```

These are CNA API rules.

Do not make binding authors infer them from Sharp Runtime internals.

---

# 69. Shutdown order

A robust shutdown sequence might conceptually be:

```text
stop new callbacks
finish pending frame
stop async work
release language-owned CNA resources
destroy game/resources
destroy graphics/audio subsystems
tear down CNA runtime
release Sharp Runtime/native globals
unload native library
```

The exact order depends on CNA implementation.

The language binding should expose a clean high-level lifecycle.

Sharp Runtime teardown details remain internal.

---

# 70. Global state

If Sharp Runtime has global/static state used by CNA, this must be hidden behind CNA initialization/shutdown.

Bindings should call:

```text
cna_runtime_initialize
cna_runtime_shutdown
```

or an equivalent high-level game lifecycle.

They should not separately initialize Sharp Runtime.

This is important for:

```text
multiple CNA instances
test isolation
DLL unload
WASM initialization
process shutdown
```

---

# 71. Multiple runtime instances

If CNA eventually supports more than one runtime/game/device instance, avoid Sharp Runtime global state becoming an accidental ABI assumption.

The C ABI should use explicit handles/contexts.

Example:

```text
CNA_Runtime
CNA_Game
CNA_GraphicsDevice
```

rather than implicit process-global objects wherever practical.

Sharp Runtime may still contain process-wide services internally, but their behavior must be compatible with CNA's documented instance model.

---

# 72. Native threading and managed runtimes

A native Sharp Runtime/CNA thread may invoke a language callback.

Each language has special requirements.

### C#

Native thread can enter managed code through supported unmanaged callback mechanisms, but lifetime/thread rules must be respected.

### Java

Native thread may need JVM attachment before invoking Java.

### Python

Native callback may need the GIL.

### JavaScript/WASM

Browser execution model is constrained by the main thread/Web Workers/WASM threading support.

### Rust

No managed runtime attachment, but Send/Sync safety matters.

Therefore the C ABI callback contract must state:

```text
which threads callbacks come from
whether caller may redirect callbacks
whether dispatch can be pumped manually
```

Sharp Runtime implementation choices cannot remain undocumented here.

---

# 73. Main-thread dispatch

A useful abstraction may be:

```text
CNA dispatcher / event pump
```

The ABI can expose:

```c
CNA_Result cna_dispatcher_pump(
    CNA_Handle runtime);
```

or ensure the game loop owns dispatch.

This can adapt native Sharp Runtime dispatcher/task behavior to languages with strict event-loop models.

Especially useful for:

```text
browser JavaScript
UI hosts
embedding scenarios
tests
```

---

# 74. Emscripten/WebAssembly

Sharp Runtime already has at least some Emscripten compile support at the native project level.

For CNA JS/WASM:

```text
TypeScript
    ↓
WASM
    ↓
CNA C ABI
    ↓
CNA C++
    ↓
Sharp Runtime components compiled into WASM where needed
```

The JS package should still expose only CNA concepts.

Any Sharp Runtime APIs unavailable under Emscripten should be handled by CNA platform capability checks, alternative implementations, or `PlatformNotSupported`-style CNA errors where appropriate.

The JS user should not debug Sharp Runtime component boundaries directly.

---

# 75. Platform capability reporting

A general CNA ABI capability API can report:

```text
renderer support
network support
media support
filesystem support
threading support
feature availability
```

Bindings can present this idiomatically.

This is preferable to exposing Sharp Runtime-specific platform macros.

Example:

```c
uint64_t cna_get_capabilities(
    CNA_Handle runtime);
```

or structured query APIs.

---

# 76. Sharp Runtime selective components

Sharp Runtime is modularized and CNA may select only required components.

This is useful for native build size and dependency management.

Language bindings should not need to know which Sharp Runtime components CNA linked.

For example:

```text
CNA minimal build
    uses Core.Base + selected components

CNA networking build
    additionally uses networking/threading components
```

The binding cares only whether the CNA feature is available.

This can be exposed through capabilities.

---

# 77. Avoid one Sharp Runtime wrapper package per language

Do not interpret CNA language bindings as a reason to create:

```text
sharp-runtime-dotnet
sharp-runtime-java
sharp-runtime-python
sharp-runtime-js
```

for CNA's sake.

C# already has .NET.

Java already has its standard library.

JS has its standard runtime.

Rust/Python/Go/Swift have their own standard libraries.

Sharp Runtime's purpose is specifically valuable in native C++.

Separate Sharp Runtime bindings could theoretically exist for independent reasons, but they are not required for CNA bindings and should not become a dependency of this plan.

---

# 78. Sharp Runtime and XNA compatibility

Sharp Runtime can help CNA implement XNA/.NET-like behavior accurately in C++.

Examples may include:

```text
strings
exceptions
collections
time
IO
delegates
tasks
serialization
```

That is useful because XNA was designed for .NET.

However, `CNA.XnaCompat` should expose real managed .NET types and semantics where possible.

This means:

```text
native side:
Sharp Runtime helps reproduce .NET-like behavior

managed side:
real .NET provides the behavior natively
```

The C ABI bridges CNA-specific data/resources.

---

# 79. Example: `Content.RootDirectory`

C#:

```csharp
Content.RootDirectory = "Content";
```

Possible flow:

```text
System.String
    ↓
CNA.NET encodes UTF-8
    ↓
cna_content_set_root_directory(...)
    ↓
CNA C++
    ↓
Sharp Runtime/native path representation
```

No `SharpRuntime.String` appears in managed code.

---

# 80. Example: native file error

Native CNA:

```text
Sharp Runtime FileStream throws FileNotFoundException-like native exception
```

C ABI boundary:

```text
catch native exception
    ↓
CNA_ERROR_IO
message = "..."
```

C#:

```text
throw ContentLoadException / IOException / CnaException
```

depending on the public CNA.NET contract.

Java:

```text
throw CnaContentException
```

Rust:

```text
Err(CnaError::Io(...))
```

The native exception class itself never crosses the boundary.

---

# 81. Example: native task

Suppose CNA internally launches work using Sharp Runtime task infrastructure.

Do not return:

```text
SharpRuntime::Task*
```

Instead:

```text
CNA_AsyncOperation handle
```

C# wrapper can return:

```csharp
Task<Asset>
```

JS:

```typescript
Promise<Asset>
```

Rust:

```rust
impl Future<Output = Result<Asset, CnaError>>
```

The binding is free to integrate with its native runtime.

---

# 82. Example: native collection

Native:

```cpp
System::Collections::Generic::List<GraphicsAdapter>
```

C ABI:

```text
adapter count
adapter-by-index
or
bulk-copy array
```

C#:

```csharp
IReadOnlyList<GraphicsAdapter>
```

Python:

```python
list[GraphicsAdapter]
```

Rust:

```rust
Vec<GraphicsAdapter>
```

No Sharp Runtime list wrapper is needed.

---

# 83. Example: callbacks

CNA C++ might internally adapt:

```text
C callback
    ↓
Sharp Runtime delegate / std::function / virtual call
```

C ABI still exposes:

```c
void (*callback)(void* context, ...);
```

C# can use an unmanaged-callable function.

Rust can use:

```rust
extern "C" fn
```

Java can use JNI/native callback machinery.

JS/WASM can use exported/imported function tables.

The native adapter hides the internal delegate type.

---

# 84. API compatibility testing involving Sharp Runtime

For .NET/XNA parity, tests can compare:

```text
real .NET/XNA expected behavior
        vs
CNA C++ implementation using Sharp Runtime
        vs
CNA.NET public result
```

This is useful because three layers may independently introduce differences.

For example:

```text
string comparison semantics
exception category
time arithmetic
collection ordering
path handling
```

The expected public behavior should be defined at the CNA/XNA compatibility level.

---

# 85. Avoid using Sharp Runtime behavior blindly

Sharp Runtime aims to emulate practical .NET behavior, but CNA should not assume that every Sharp Runtime implementation detail is automatically the correct XNA behavior.

For XNA compatibility:

```text
XNA behavior
```

is the target.

Sharp Runtime is a tool used to implement it.

If Sharp Runtime differs in a corner case relevant to XNA, CNA may need an adaptation layer.

The stable C ABI and bindings should follow CNA's intended semantics.

---

# 86. Documentation layering

Recommended documentation split:

## Sharp Runtime docs

Explain:

```text
C++ API
components
.NET-like semantics
native portability
component dependency graph
```

## CNA C++ docs

Explain:

```text
CNA framework
XNA compatibility
how CNA uses Sharp Runtime where relevant
```

## CNA C ABI docs

Explain:

```text
handles
ownership
UTF-8
errors
callbacks
threading
buffers
versioning
capabilities
```

Do not require reading Sharp Runtime docs to understand the public C ABI.

## Binding docs

Explain:

```text
language package
native loading
resource lifecycle
language-specific API
XNA compatibility
performance
```

---

# 87. Public messaging

A concise explanation for users could be:

> CNA is implemented in C++ and uses Sharp Runtime internally for selected .NET-like native facilities. Language bindings communicate with CNA through a stable C ABI; they do not expose Sharp Runtime directly. C# uses the real .NET libraries, while other bindings use the native standard facilities of their language.

This avoids confusion that:

```text
CNA.NET somehow runs on Sharp Runtime
```

It does not.

---

# 88. Possible naming confusion

Because the project is called **Sharp Runtime**, some users may assume it is required to execute C# CNA games.

Documentation should explicitly say:

```text
Sharp Runtime is a C++ library.
It is not the .NET CLR.
It does not execute C# assemblies.
CNA.NET applications run on a normal supported .NET runtime.
```

CNA's native binary may internally contain or link Sharp Runtime code.

That is invisible to ordinary C# users.

---

# 89. Native AOT

If a C# CNA application is later published using .NET Native AOT:

```text
C# app
    ↓
native .NET application
    ↓
CNA C ABI
    ↓
CNA C++
```

the architecture remains valid.

Sharp Runtime still remains inside CNA's native implementation.

The two native worlds are separate libraries/components connected through the C ABI.

This is another benefit of avoiding C++/CLI as the primary bridge.

---

# 90. Embedding CNA in other runtimes

The same isolation allows:

```text
Unity tooling
Godot native extension
Python host
Java desktop host
browser host
server runtime
```

to embed CNA without importing Sharp Runtime's public object model.

Only CNA's stable ABI needs to be understood.

---

# 91. Plugin systems

If CNA later exposes native plugins, avoid requiring language bindings to understand Sharp Runtime plugin interfaces unless the plugin itself is explicitly a C++-only API.

For cross-language plugins:

```text
stable CNA plugin C ABI
```

should be used.

Sharp Runtime can still implement helper facilities internally.

---

# 92. Binary compatibility

The C ABI is the binary compatibility contract for bindings.

Sharp Runtime C++ ABI is **not** the binding compatibility contract.

This distinction is essential across:

```text
compiler versions
standard libraries
platforms
linker configurations
Sharp Runtime refactors
```

Bindings should survive native implementation changes as long as CNA ABI compatibility is preserved.

---

# 93. Symbol visibility

Native builds should use visibility rules so the intended exported interface is clear.

Conceptually:

```text
public:
cna_*

private/hidden:
most CNA C++ implementation symbols
most Sharp Runtime symbols
renderer internals
STL/template implementation symbols
```

Exact symbol policy depends on static/shared linking and platform.

The goal is a clean externally inspectable ABI.

---

# 94. Security and robustness

The C ABI must validate foreign inputs because bindings can accidentally or maliciously pass invalid values.

Examples:

```text
invalid handles
oversized lengths
null pointers
misaligned buffers
invalid UTF-8 if required
out-of-range enum values
stale callbacks
double release
thread misuse
```

Sharp Runtime may provide exceptions and validation helpers internally, but the public boundary must convert failures into deterministic CNA errors rather than crashes or uncaught exceptions.

---

# 95. Fuzzing

The C ABI is an excellent fuzzing target.

Possible fuzz targets:

```text
string APIs
content metadata
buffer upload
handle operations
enum decoding
serialized formats
network packet APIs
callback registration
```

This can indirectly test CNA and Sharp Runtime integration without exposing Sharp Runtime itself.

---

# 96. ABI stability policy

Suggested policy:

```text
CNA C++ API:
may evolve according to CNA version policy

Sharp Runtime API:
evolves according to Sharp Runtime policy

CNA C ABI:
strong compatibility guarantee within ABI major version

language bindings:
declare compatible CNA ABI range
```

This separates three different kinds of versioning that should not be conflated.

---

# 97. What happens when Sharp Runtime changes incompatibly

Example:

```text
Sharp Runtime 0.x → 1.x
```

If CNA updates to the new version:

```text
1. CNA C++ integration changes
2. CNA native tests run
3. CNA C ABI tests run
4. ABI symbol/signature checks run
5. binding parity tests run
```

If the CNA C ABI did not semantically change:

```text
no language binding API release should be required
```

except perhaps to consume newly built native binaries.

This is the ideal result.

---

# 98. What happens when CNA semantics change

If CNA itself changes an API semantic or exposes a new feature:

```text
CNA C++ API change
    ↓
possibly CNA C ABI extension
    ↓
binding update
```

Sharp Runtime may or may not be involved internally.

The externally relevant event is the CNA ABI change.

---

# 99. What happens when a new Sharp Runtime component is adopted

Suppose CNA starts using a new Sharp Runtime component for compression or networking.

The binding should ideally see:

```text
no API change
```

unless CNA exposes a new capability.

This means Sharp Runtime modularization benefits native builds without multiplying language-binding maintenance.

---

# 100. Sharp Runtime does not need a C ABI for CNA bindings

It might be tempting to create:

```text
Sharp Runtime C ABI
    ↓
CNA C ABI
```

This is not necessary for CNA binding architecture.

CNA can call Sharp Runtime directly in C++.

Only CNA's external boundary needs a stable C ABI.

Creating a separate complete Sharp Runtime C ABI would be a large independent project and should only exist if Sharp Runtime itself has a standalone cross-language goal.

It should not be a prerequisite for CNA.NET or any other CNA binding.

---

# 101. The C ABI should represent CNA, not `.NET System.*`

Do not make CNA's stable C API look like:

```text
system_string_create
system_list_add
system_task_wait
```

unless CNA specifically needs those concepts as part of its own public framework.

The C ABI should be domain-oriented:

```text
cna_game_*
cna_graphics_device_*
cna_texture2d_*
cna_sprite_batch_*
cna_content_*
cna_keyboard_*
cna_audio_*
```

This keeps the boundary focused and prevents accidental recreation of an enormous general-purpose runtime API.

---

# 102. Why this matters strategically

Without this separation, adding language bindings could unintentionally turn into two huge projects:

```text
bind CNA
+
bind all of Sharp Runtime
```

That would greatly increase scope.

With the correct boundary:

```text
bind CNA only
```

Sharp Runtime remains an implementation detail.

This is a major scope-control mechanism.

---

# 103. Relationship to multi-language expansion

The stable boundary now becomes:

```text
                    CNA C++
                      │
        ┌─────────────┴─────────────┐
        ↓                           ↓
 Sharp Runtime                renderer/native libs
        │                           │
        └─────────────┬─────────────┘
                      ↓
                 CNA C ABI
                      ↓
     ┌────────────────┼─────────────────┐
     ↓                ↓                 ↓
    .NET             JS                Rust
     ↓                ↓                 ↓
   Python           Java              Zig
```

This architecture allows CNA's native implementation to continue benefiting from Sharp Runtime while language reach expands independently.

---

# 104. Recommended implementation tasks related specifically to Sharp Runtime

When implementing the CNA C ABI, explicitly audit every public CNA C++ signature for Sharp Runtime types.

Create a classification such as:

```text
TYPE                      ABI MAPPING
--------------------------------------------------
System::String            UTF-8 string
System::TimeSpan          fixed tick/duration value
System::DateTime          explicit timestamp representation
List<T>                   count/copy or array view
Dictionary<K,V>           explicit lookup/enumeration API
Stream                    CNA stream handle/callback abstraction
Task<T>                   CNA async operation abstraction
Exception                 CNA_Result + error info
Delegate                  function pointer + context
Span<T>                   pointer + count
Memory<T>                 buffer abstraction
Uri                       UTF-8 canonical URI string or CNA URI struct
```

This mapping table should become a formal design artifact.

---

# 105. Suggested new CNA documentation

When C ABI work begins, create something like:

```text
docs/c-api/sharp-runtime-boundary.md
```

It should specify:

```text
Sharp Runtime is native-only
forbidden ABI leaks
type mapping table
ownership conversion rules
exception conversion
string conversion
stream conversion
task conversion
delegate conversion
testing rules
```

This document can be derived from this addendum.

---

# 106. Suggested static checker

A future checker can inspect C ABI headers and reject prohibited tokens.

Example pseudo-policy:

```text
FORBIDDEN IN modules/c-api/include:

SharpRuntime
System::
std::
template
class
namespace
throw
reference types
C++-only attributes
```

Some C-compatible `bool` policy may also be restricted in favor of explicit integer types.

The authoritative check should compile headers with a real C compiler.

---

# 107. Suggested integration test matrix

Test configurations should include different Sharp Runtime component selections where possible.

For example:

```text
minimal/headless CNA C ABI
graphics CNA C ABI
network-enabled CNA C ABI
media/audio-enabled CNA C ABI
Emscripten CNA C ABI
Windows CNA C ABI
Linux CNA C ABI
```

The language binding should see the same ABI contract or explicit capability differences.

This validates that optional Sharp Runtime dependencies do not accidentally leak into ABI structure.

---

# 108. Cross-platform concerns

Sharp Runtime has some platform-specific feature boundaries.

The CNA C ABI should absorb these differences behind:

```text
capability queries
well-defined unsupported errors
feature flags
```

Bindings should not directly parse Sharp Runtime platform macros.

Example:

```text
feature unavailable natively
    ↓
CNA_ERROR_NOT_SUPPORTED
```

C# can map to:

```csharp
PlatformNotSupportedException
```

Java:

```java
UnsupportedOperationException
```

Rust:

```rust
Err(CnaError::NotSupported)
```

---

# 109. MSVC and special numeric types

If Sharp Runtime has reduced support for types requiring compiler-specific 128-bit integers on certain toolchains, do not let this create an accidental ABI difference in unrelated CNA APIs.

Either:

```text
exclude such functionality from the CNA ABI
```

or define a platform-independent explicit representation.

The ABI layout must not silently depend on:

```text
compiler-native __int128 availability
```

unless carefully specified.

---

# 110. Emscripten platform differences

Some Sharp Runtime APIs may deliberately be unsupported under Emscripten.

For the CNA JS/WASM binding:

```text
CNA feature
    ↓
native capability
    ↓
supported / unsupported
```

must be exposed at the CNA level.

Do not expose:

```text
"Sharp Runtime component X is unavailable"
```

as the primary user-facing concept.

Say:

```text
"CNA feature Y is not supported on this platform."
```

The native reason can still appear in diagnostics.

---

# 111. Testing against real .NET behavior

Sharp Runtime's .NET-like implementation can be useful in CNA, but C# XNA compatibility has a unique advantage:

the managed side can directly use the real .NET BCL.

Compatibility tests can compare:

```text
real .NET behavior
vs
Sharp Runtime/native behavior
```

for relevant shared concepts.

Examples:

```text
string operations
path normalization
TimeSpan arithmetic
exception categories
collection edge cases
formatting
```

This can improve CNA's native behavioral fidelity.

---

# 112. Do not overuse Sharp Runtime merely for symmetry

Because CNA.NET uses real .NET, it may be tempting to make native CNA use Sharp Runtime for every possible operation just to mirror it.

That should not become a goal by itself.

Use Sharp Runtime where it provides:

```text
correctness
portability
API compatibility
maintainability
reuse
```

Do not replace a simpler native implementation solely to make the architecture look symmetrical.

The stable ABI makes internal choice flexible.

---

# 113. Sharp Runtime as a development accelerator for CNA

Where useful, Sharp Runtime reduces the need for CNA to reinvent:

```text
collections
IO
text
JSON/XML
threading helpers
network primitives
time
exceptions
```

This can indirectly accelerate language-binding work because CNA's native implementation becomes more complete.

But the binding project should still remain scoped to:

```text
CNA API
```

not:

```text
all Sharp Runtime APIs
```

---

# 114. Sharp Runtime and generated bindings

If binding generation examines CNA's C++ API directly, it must understand Sharp Runtime types semantically.

An even better architecture is:

```text
CNA C++ API
    ↓
hand-curated/validated C ABI schema
    ↓
binding generation
```

rather than:

```text
CNA C++ headers
    ↓
blind auto-generator
    ↓
language bindings
```

Sharp Runtime usage is another strong reason for the curated ABI layer.

---

# 115. One canonical conversion layer

Do not implement Sharp Runtime-to-language conversions separately in each binding.

Bad:

```text
C# binding understands System::String
Rust binding understands System::String
Java binding understands System::String
```

Good:

```text
CNA native C ABI adapter understands System::String once
        ↓
UTF-8 CNA ABI
        ↓
all bindings consume UTF-8
```

The same for:

```text
collections
streams
exceptions
tasks
delegates
```

This centralizes complexity.

---

# 116. Debugging boundaries

When a binding bug occurs, diagnostics should help identify the layer:

```text
language API
language interop
C ABI
CNA C++
Sharp Runtime
renderer/platform
```

Possible error logging:

```text
CNA ABI function: cna_content_load_texture2d
native error category: IO
native message: ...
native subsystem: content
```

It may optionally include deeper diagnostic origin such as Sharp Runtime in debug builds, but public behavior should remain CNA-level.

---

# 117. ABI tracing

A debug build could provide optional tracing:

```text
C# call
→ cna_texture2d_create
→ handle 0x...
→ CNA Graphics
→ native resource
```

If an internal exception originates in Sharp Runtime, the trace can record that internally.

This would be extremely helpful for debugging FFI failures without exposing Sharp Runtime as part of the public type system.

---

# 118. Crash boundaries

Not all native failures can be converted to errors.

Examples:

```text
segmentation fault
stack corruption
undefined behavior
GPU driver crash
native assertion abort
```

The binding architecture should minimize these through testing, but a C ABI cannot make native code memory-safe automatically.

Sharp Runtime may reduce some categories of implementation error but does not remove the need for native sanitizers and robust CNA testing.

---

# 119. Documentation for binding authors

A binding author should be told:

```text
You bind CNA C ABI only.
Do not include Sharp Runtime headers.
Do not call Sharp Runtime C++ symbols.
Do not depend on Sharp Runtime object layout.
Do not assume CNA native ownership maps to your GC.
Use CNA error/ownership/threading contracts.
```

This should be a hard rule for official bindings.

---

# 120. Official versus experimental bindings

Official bindings should obey the stable C ABI.

An experimental C++-specific integration may use CNA/Sharp Runtime internals for tooling, but it should not be branded as a stable language binding.

This distinction protects the long-term ecosystem.

---

# 121. Native extensions

Some advanced users may want low-level native escape hatches.

If CNA eventually exposes renderer/native handles:

```text
Vulkan device
D3D device
SDL objects
platform window
```

those should be explicit **CNA native extension APIs**.

They still should not expose unrelated Sharp Runtime implementation details.

Extension APIs may have weaker stability guarantees and should be versioned separately if needed.

---

# 122. Sharp Runtime and scripting

If CNA later supports scripting languages, the same architecture remains:

```text
script language
    ↓
binding
    ↓
CNA C ABI
    ↓
CNA C++
    ↓
Sharp Runtime internally
```

There is no need for scripts to bind Sharp Runtime unless Sharp Runtime itself becomes an independent product with its own scripting goal.

---

# 123. Long-term modularity

A mature CNA build may select:

```text
CNA runtime subset
+
specific renderers
+
specific Sharp Runtime components
```

for a given platform.

For example:

```text
browser build
desktop build
headless server build
mobile build
```

The stable C ABI can expose only available capabilities or maintain a superset that returns not-supported errors.

Language binding source code can remain mostly unchanged.

---

# 124. Potential size concerns

Sharp Runtime plus CNA plus many renderers may make native binaries large.

Bindings should not solve this by bypassing Sharp Runtime.

Instead use:

```text
component selection
link-time optimization
dead-code elimination
renderer selection
feature flags
separate runtime packages
WASM size optimization
```

The architecture should keep API and build-size concerns separate.

---

# 125. WebAssembly size

For `cna-js`, Sharp Runtime components should be selected narrowly.

Avoid linking unused modules such as:

```text
networking
XML
compression
threading
```

unless CNA browser functionality requires them.

The binding API remains stable.

This is one of the strongest practical benefits of Sharp Runtime's modular design.

---

# 126. `CNA.XnaCompat` and Sharp Runtime names

`CNA.XnaCompat` should use Microsoft's familiar namespace model where legally and technically appropriate for compatibility:

```text
Microsoft.Xna.Framework
Microsoft.Xna.Framework.Graphics
Microsoft.Xna.Framework.Input
...
```

It should not expose:

```text
SharpRuntime.*
```

Sharp Runtime naming belongs to the native project, not the managed compatibility facade.

---

# 127. `CNA.Framework` and Sharp Runtime

The more idiomatic future CNA.NET API:

```text
CNA.Framework
```

also should not expose Sharp Runtime.

It may expose modern CNA-specific concepts independent of XNA.

This preserves freedom for CNA to evolve.

---

# 128. Native C++ applications remain different

A C++ CNA user may intentionally include and use Sharp Runtime because it is useful.

Example:

```cpp
#include <Microsoft/Xna/Framework/Game.hpp>
#include <System/String.hpp>
```

This is fine in native C++.

The strict isolation rule applies to:

```text
stable cross-language C ABI
```

not to all C++ consumers.

---

# 129. Possible optional direct Sharp Runtime use by third-party C++ tools

A C++ editor or tool could use both:

```text
CNA
Sharp Runtime
```

directly.

That is not a language-binding architecture issue.

It remains a normal native C++ dependency choice.

Do not generalize that model to managed bindings.

---

# 130. Why the name "runtime" can be misleading to binding users

Many developers hear "runtime" and think:

```text
CLR
JVM
JavaScript VM
Python interpreter
```

Sharp Runtime is different.

Documentation for CNA bindings should explicitly state:

> Sharp Runtime is a C++ library implementing .NET-like APIs for native code. It is not the runtime that executes CNA.NET applications.

This one sentence prevents substantial confusion.

---

# 131. Recommended README paragraph for `cna-dotnet`

A future README could say:

> CNA.NET is a managed frontend for the native CNA engine. CNA itself is implemented in C++ and may use Sharp Runtime internally. CNA.NET applications run on the normal .NET runtime and use the real .NET Base Class Library; Sharp Runtime is not exposed through the managed API. Native interoperability is provided through CNA's stable C ABI.

This is concise and accurate.

---

# 132. Recommended README paragraph for `cna-js`

> The JavaScript/TypeScript binding communicates with CNA through its stable native/WebAssembly ABI. CNA's C++ implementation may use Sharp Runtime internally, but Sharp Runtime APIs are not part of the JavaScript surface.

---

# 133. Recommended README paragraph for `cna-rs`

> `cna-rs` wraps CNA's stable C ABI. The safe Rust layer uses Rust-native ownership, errors, strings, and collections. CNA's internal use of Sharp Runtime is intentionally hidden behind the ABI.

---

# 134. Recommended README paragraph for `cna-java`

> The Java binding uses Java-native types and standard-library facilities and calls the native CNA engine through its stable interoperability ABI. Sharp Runtime is an internal C++ dependency of CNA, not a Java runtime requirement.

---

# 135. Compatibility responsibility

Sharp Runtime is responsible for:

```text
its own C++ API correctness
its .NET-like semantics
its platform support
its component boundaries
```

CNA is responsible for:

```text
CNA/XNA semantics
native subsystem behavior
C ABI stability
conversion from Sharp Runtime/native types
```

Bindings are responsible for:

```text
language idioms
marshalling
resource wrappers
package management
runtime integration
```

Keeping these responsibilities separate prevents architectural drift.

---

# 136. Bug ownership examples

### Bug: `System::String` native formatting is wrong

Likely Sharp Runtime issue.

### Bug: `Content.Load<Texture2D>` maps wrong native error to C#

Likely CNA C ABI / CNA.NET issue.

### Bug: texture is destroyed twice after `Dispose()`

Likely binding ownership or C ABI handle issue.

### Bug: XNA game renders differently because blend semantics differ

Likely CNA graphics/XNA compatibility issue.

### Bug: C# application cannot find native DLL

Likely CNA.NET packaging/interoperability issue.

This separation is useful for project maintenance.

---

# 137. API review checklist for Sharp Runtime leakage

Before adding a C ABI function, ask:

1. Does its signature expose any C++ type?
2. Does it expose any Sharp Runtime type?
3. Does it assume Sharp Runtime ownership?
4. Does it assume Sharp Runtime exception hierarchy?
5. Does it expose a container layout?
6. Does it expose a string layout?
7. Does it expose task/delegate internals?
8. Can it be represented as POD/handle/buffer/callback instead?
9. Can the internal implementation change without changing this ABI?
10. Can a pure C compiler consume the header?

If the answer to #10 is no, the interface is not a proper CNA C ABI.

---

# 138. Release-gate checklist

Before releasing a new CNA ABI version:

```text
[ ] pure C headers compile
[ ] no Sharp Runtime symbols in public C ABI headers
[ ] C++ exceptions are caught
[ ] ABI version is correct
[ ] structs have stable explicit layouts
[ ] ownership documentation updated
[ ] UTF-8 conventions followed
[ ] callback thread/lifetime rules documented
[ ] binding smoke tests pass
[ ] symbol export audit passes
[ ] Sharp Runtime update did not change ABI accidentally
```

---

# 139. First implementation milestone involving Sharp Runtime

When building the first C ABI prototype, deliberately choose APIs that touch native types likely backed by Sharp Runtime.

For example:

```text
Game lifecycle
window title string
Content.RootDirectory
keyboard state
texture loading
error reporting
```

This tests:

```text
string conversion
exceptions
paths
handles
callbacks
value structs
```

early.

Do not postpone all Sharp Runtime boundary problems until the API becomes large.

---

# 140. Suggested prototype

A first C# program:

```csharp
public sealed class HelloGame : Game
{
    protected override void LoadContent()
    {
        Content.RootDirectory = "Content";
        texture =
            Content.Load<Texture2D>("player");
    }

    protected override void Draw(
        GameTime gameTime)
    {
        GraphicsDevice.Clear(
            Color.CornflowerBlue);
    }
}
```

exercises:

```text
C# strings
C ABI UTF-8
native CNA content
Sharp Runtime path/IO internally
native error conversion
graphics resource handles
GameTime value conversion
callbacks
```

This is an excellent boundary test.

---

# 141. Strategic conclusion

Sharp Runtime strengthens CNA's native implementation but should **not** broaden the public language-binding surface.

The cleanest model is:

```text
Sharp Runtime
    =
native C++ implementation support

CNA C ABI
    =
stable interoperability boundary

CNA.NET / cna-js / cna-rs / ...
    =
language-native public frontends
```

This separation lets each project do what it is best at.

Sharp Runtime provides reusable .NET-like facilities in C++.

CNA provides the game/framework runtime and XNA-compatible behavior.

The C ABI provides stable binary interoperability.

Bindings provide idiomatic language-specific APIs.

---

# 142. Final architecture

```text
                           USER CODE

        C#            TypeScript          Rust
        │                 │                │
        ↓                 ↓                ↓
   CNA.XnaCompat     @openeggbert/cna      cna
        │                 │                │
   CNA.Framework        JS/WASM          cna-sys
        │                 │                │
   CNA.Interop            └───────┬────────┘
        │                         │
        └─────────────────────────┤
                                  ↓
                           CNA STABLE C ABI
                                  ↓
                             CNA C++ CORE
                                  │
                ┌─────────────────┼──────────────────┐
                ↓                 ↓                  ↓
           Sharp Runtime      CNA subsystems      renderers
                │                 │                  │
                └─────────────────┼──────────────────┘
                                  ↓
                           NATIVE PLATFORM
```

Additional languages:

```text
Python
Java
Zig
Go
Swift
...
```

join at:

```text
CNA stable C ABI
```

not at:

```text
Sharp Runtime
```

---

# 143. Final decisions to preserve

1. **Sharp Runtime remains a native C++ project and dependency.**
2. **Sharp Runtime is not a CLR and does not execute CNA.NET applications.**
3. **C# CNA applications run on the real .NET runtime and real `System.*` libraries.**
4. **Other language bindings use their own native standard libraries and idioms.**
5. **The CNA stable C ABI must hide Sharp Runtime completely.**
6. **No Sharp Runtime object, collection, exception, task, delegate, stream, or string layout should leak through the stable ABI.**
7. **Convert strings to UTF-8 at the ABI.**
8. **Convert collections to arrays/count-copy/enumeration APIs.**
9. **Convert exceptions to `CNA_Result` plus structured error details.**
10. **Convert tasks to a CNA-neutral async-operation model.**
11. **Convert delegates to C function pointers plus context.**
12. **Convert streams to CNA stream handles or callback interfaces.**
13. **Bindings depend on CNA ABI versions, not Sharp Runtime versions.**
14. **A Sharp Runtime internal refactor should not break bindings if CNA semantics and ABI remain stable.**
15. **C ABI headers must compile as real C and should be validated against accidental Sharp Runtime/C++ leakage.**
16. **CNA owns the conversion layer once; individual bindings must not each understand Sharp Runtime internals.**
17. **Sharp Runtime component modularity can reduce native build size without changing binding APIs.**
18. **CNA.NET, cna-js, cna-rs, cna-python, cna-java, and future bindings remain separate language-facing projects over the same CNA ABI.**
19. **The presence of Sharp Runtime does not justify binding or reimplementing the entire `.NET System.*` surface in every language.**
20. **The goal is to expose CNA, not Sharp Runtime, to the multi-language ecosystem.**

---

# 144. Final recommendation

Treat Sharp Runtime as an important **native implementation asset** of CNA, not as an interoperability surface.

The most durable architecture is:

```text
language-native application
        ↓
language-native CNA API
        ↓
stable CNA C ABI
        ↓
CNA C++ implementation
        ↓
Sharp Runtime and other native dependencies
```

This allows CNA to benefit heavily from Sharp Runtime while preserving:

```text
ABI stability
language independence
clean packaging
simple documentation
lower maintenance cost
freedom to refactor native internals
```

It also prevents the language-binding effort from accidentally expanding into a second enormous project that attempts to bind all of Sharp Runtime.

In short:

> **Sharp Runtime should help implement CNA. The CNA C ABI should hide Sharp Runtime. The bindings should expose CNA.**
