# CNA Binding Language Landscape

**Status:** Proposal  
**Date:** 2026-08-14  
**Companion documents:** `analysis_binding.md`, `analysis_binding_sharp_runtime.md`

## Purpose

CNA should keep one canonical C++ implementation and expose other languages
through a stable CNA C ABI. There is no finite list of *all* possible target
languages: any language with a practical C FFI can use that ABI. This document
lists the language families for which an official binding is technically
possible and proposes a deliberately small implementation order.

```text
Language-native CNA API
          ↓
Language interop layer
          ↓
Stable CNA C ABI
          ↓
Canonical CNA C++ implementation
          ↓
Sharp Runtime, CNA subsystems, and renderers
```

Sharp Runtime is an internal C++ implementation dependency. No binding should
expose or depend on its C++ types, layouts, ownership rules, or version.

## Shared ABI requirements

Every binding should use the same ABI rules:

- opaque, generation-checked native handles with explicit release operations;
- fixed-width primitive types, plain ABI structs, and explicit struct versions;
- UTF-8 strings expressed as pointer-plus-length values;
- `CNA_Result` plus structured error information instead of C++ exceptions;
- callbacks expressed as C function pointers plus caller context;
- explicit ownership, threading, async, and shutdown contracts;
- bulk commands and snapshots for performance-sensitive traffic; and
- an ABI version check at binding initialization.

The public ABI represents CNA concepts such as games, graphics devices,
textures, input, audio, and content. It must not become a wrapper for the
general-purpose `System.*` surface provided internally by Sharp Runtime.

## Proposed support order

| Priority | Binding family | Main reason |
|---|---|---|
| 1 | .NET, led by C# | XNA was a C# framework; this has the strongest source-compatibility value. |
| 2 | JavaScript and TypeScript | WebAssembly gives browser reach and TypeScript maps well to XNA concepts. |
| 3 | Rust | Natural C ABI integration and strong resource-lifetime modelling. |
| 4 | Python | Useful for education, tooling, scripts, prototypes, and tests. |
| 5 | Java/Kotlin and the JVM | Large ecosystem and a natural object-oriented mapping. |
| 6 | Zig, Go, and Swift/Objective-C | Good native/platform integrations, but smaller immediate CNA demand. |
| 7 | All other families | Add only after a concrete maintainer, user demand, and a real compatibility target exist. |

The ordering is an investment decision, not a technical limitation. A stable
C ABI makes the later bindings possible without duplicating CNA.

## Candidate language families

| Family | Languages that can share a binding or ABI layer | Recommended approach | Status |
|---|---|---|---|
| C and C++ | C; existing C++ users | Ship the C ABI headers and library in CNA; C++ continues to use the native API. | Foundation |
| .NET | C#, F#, Visual Basic .NET, PowerShell | One `cna-dotnet` package. Build the high-fidelity `CNA.XnaCompat` facade in C#; expose idiomatic .NET APIs to the other languages. | First official binding |
| Web | TypeScript, JavaScript, Node.js | `cna-js` over WebAssembly first, with browser WebGL/WebGPU support; consider a native Node N-API path only when needed. | Second official binding |
| Rust | Rust | `cna-sys` generated/raw ABI declarations plus a safe `cna` crate with RAII/`Drop` resource wrappers. | Early candidate |
| Python | CPython; potentially PyPy where its extension ABI permits | `cna-python` with normal Python objects, properties, exceptions, and explicit `dispose`/context-manager support. | Early candidate |
| JVM | Java, Kotlin, Scala, Groovy, Clojure | One Java-native layer using JNI, Panama, or a successor supported by the target JDK; JVM languages reuse it. | Demand-driven |
| Native systems | Zig, Go, Swift, Objective-C, D, Nim, Crystal, Haxe | Bind the C ABI directly, then provide a small idiomatic wrapper. Swift/Objective-C are particularly relevant to Apple targets. | Demand-driven |
| Scripting and technical computing | Lua/LuaJIT, Ruby, PHP, Perl, Julia, R, Tcl | Use a native extension or the language's C FFI. Prefer scripting/tooling APIs before promising a complete game-framework facade. | Demand-driven |
| Traditional native languages | Delphi/Object Pascal/Free Pascal, Ada, Fortran | Use C-compatible declarations and a thin wrapper. These are feasible where a maintainer needs them. | Community-maintained |
| Functional and Lisp languages | OCaml, Haskell, Common Lisp, Scheme/Racket, Erlang/Elixir | Use each runtime's C FFI/NIF facility. Carefully define callbacks, garbage collection roots, and scheduler/thread rules. | Community-maintained |

Other FFI-capable languages belong in the same final category. A separate
official repository is justified only when it has an owner, automated ABI
tests, packaging, documentation, and at least one real CNA application.

## Binding design by language style

The binding should preserve CNA/XNA *concepts*, not mechanically reproduce C#
syntax in every language.

| Style | Examples | Public API direction |
|---|---|---|
| XNA-compatible OO | C# | Preserve XNA 4.0 names and inheritance as closely as practical. |
| Idiomatic OO | Java, Kotlin, Swift, Python, Ruby | Use normal properties, exceptions, and lifecycle conventions for that language. |
| Ownership-oriented | Rust, Zig, C++, D, Nim | Keep opaque handles private and model native resources with RAII, `Drop`, or explicit deinitialization. |
| Interface/callback-oriented | Go, C, Lua, functional languages | Adapt the `Game` lifecycle to interfaces, records, or callback tables instead of forcing inheritance. |
| Web/WASM | TypeScript, JavaScript | Keep math values local to JS and batch drawing/resource commands across the JS/WASM boundary. |

Math and other pure value types should normally be implemented locally in the
target language. Native-backed objects such as `Texture2D`, `SpriteBatch`,
`GraphicsDevice`, effects, models, and audio resources should own or reference
ABI handles.

## Platform and runtime boundaries

A language binding does not automatically make every CNA renderer or platform
available. Support remains the intersection of:

```text
binding implementation
∩ CNA subsystem support
∩ selected renderer/platform support
∩ target runtime packaging support
```

Examples:

- `cna-js` depends on the supported browser/WASM renderer path.
- Swift and Objective-C bindings depend on the Apple-native CNA stack.
- JVM, Python, and Node native packages need per-platform binary delivery.
- Garbage-collected runtimes need explicit native-resource disposal and safe
  callback rooting; finalizers are only a fallback.

## Acceptance rule for an official binding

Before naming a binding official, it should provide all of the following:

1. ABI-version validation and automated ABI smoke tests.
2. Correct string, error, handle, callback, and shutdown behaviour.
3. Native package loading on its supported platforms.
4. A real game loop that clears, loads a texture, draws with `SpriteBatch`,
   reads input, and exits cleanly.
5. Documentation that states its XNA-compatibility and platform limits
   accurately.
6. A maintainer responsible for releases and compatibility testing.

## Recommendation

Implement and stabilize the CNA C ABI in this repository first. Then deliver
`cna-dotnet` for C#/.NET, followed by `cna-js` for TypeScript/JavaScript and
then Rust and Python. Treat Java/Kotlin, Zig, Go, Swift/Objective-C, and the
remaining C-FFI ecosystems as compatible future targets rather than parallel
initial projects.

This approach grows CNA's language reach while retaining one engine,
one renderer stack, one error/ownership model, and one stable interoperability
boundary.
