# CNA Native C Binding / Stable C ABI — Implementation Plan

> **Status: PLANNED — no implementation authorized or started (2026-08-14).** This document is
> the plan for a native C API, implemented inside the main CNA repository. It is intentionally
> not a plan for C#, .NET, JavaScript/TypeScript, Rust, Python, Java, Zig, Go, Swift, or any other
> language-specific binding. Such work must not begin, nor be planned here, without a new explicit
> owner instruction.

> **Authoritative design inputs (read-only):** `analysis_binding.md` and
> `analysis_binding_sharp_runtime.md`. The behavioral reference for the underlying XNA-facing C++
> implementation remains the local FNA tree required by `AGENTS.md`.

## Goal

Expose a deliberately small, documented and testable **C ABI** over canonical CNA C++.

```text
C application
    ↓
CNA public C headers and C ABI
    ↓
canonical CNA C++ implementation
    ↓
Sharp Runtime and native renderer/platform dependencies
```

The C API is a first-class CNA public interface, not a mechanical export of the C++ ABI and not a
separate `cna-c` repository. It must be able to evolve atomically with the CNA modules it adapts.

## Scope and non-goals

In scope:

- a C-compatible, versioned ABI inside this repository;
- C applications that link the native CNA library and use only public C headers;
- the C API's own lifecycle, graphics, input, content, audio, data-transfer and callback contracts;
- C-only compile/link/runtime tests, native adapter tests, documentation, export inspection and
  supported-platform packaging.

Out of scope:

- a language-specific binding, wrapper, package, generator or sample for any language other than C;
- a separate C engine or a second implementation of CNA;
- exporting arbitrary C++ classes, Sharp Runtime, STL, renderer-private or platform-private objects;
- promising a complete one-to-one C projection of every current or future XNA/CNA C++ member;
- declaring ABI 1.0 before experimental releases are exercised by real C applications.

## Non-negotiable ABI invariants

1. CNA C++ remains the sole canonical implementation; the C API is an adapter layer.
2. The public header surface compiles as real C without C++ mode, C++ headers, templates, namespaces,
   references, exceptions, RTTI, `std::*`, `System::*`, or Sharp Runtime names.
3. Every fallible entry point returns `CNA_Result`; no C++ or Sharp Runtime exception may cross the
   ABI boundary.
4. Public primitives have fixed-width representations. The design must not expose `long`,
   `unsigned long`, `wchar_t`, compiler-dependent enums, raw C++ `bool`, or implementation-defined
   ownership.
5. Text is explicit UTF-8 bytes plus a fixed-width length. Returned text has a documented lifetime
   and uses a caller-buffer/copy contract unless a separately reviewed ownership design says
   otherwise.
6. C++ objects and raw pointers never cross the ABI. Resource identity uses validated opaque handles
   with stale-handle detection.
7. Every handle parameter documents whether it is owned, borrowed, transferred, nullable, or valid
   only during a callback. Double release, invalid kind and stale generation fail deterministically.
8. Plain ABI structs are layout-versioned when extensible, explicitly initialized, and covered by
   C and C++ layout tests. New fields are appended only under the documented versioning rules.
9. Callback signatures use C function pointers plus an opaque context pointer. Their thread,
   re-entrancy, lifetime, cancellation and shutdown rules are contractual, not inferred.
10. Sharp Runtime is strictly native-only. The C API adapts its strings, collections, streams,
    exceptions, delegates and time types once into CNA-neutral forms.
11. Renderer selection remains CNA-native. The C ABI reports the selected renderer and capabilities;
    it does not invent a parallel renderer system.
12. High-frequency operations transfer data in bulk. The initial API must not force per-pixel,
    per-vertex, per-sprite, or per-key FFI calls.

## Proposed repository shape

The exact filenames remain subject to the first design gate, but implementation belongs in the
existing physical module layout:

```text
modules/c-api/
├── CMakeLists.txt
├── include/CNA/C/
│   ├── cna.h                 # umbrella only
│   ├── abi.h                 # version, export, result, fixed ABI primitives
│   ├── core.h                # handles, errors, strings, buffers, capabilities
│   ├── runtime.h             # instance/game lifecycle and callbacks
│   ├── graphics.h            # graphics/device/2D resources and batches
│   ├── input.h               # snapshot input APIs
│   ├── content.h             # content/root-directory APIs
│   └── audio.h               # only after its explicit phase is approved
├── src/
├── tests/
│   ├── pure_c/
│   └── cpp/
└── examples/                 # C-only examples, only after the test foundation is green

docs/c-api/
├── README.md
├── ABI_VERSIONING.md
├── HANDLES.md
├── OWNERSHIP.md
├── ERRORS.md
├── STRINGS_AND_BUFFERS.md
├── CALLBACKS_AND_THREADING.md
├── RENDERERS_AND_CAPABILITIES.md
└── SHARP_RUNTIME_BOUNDARY.md
```

The module's exported target and CMake options must be selected during `CBIND-007`; the plan does
not presume that the current header-only `CNA` aggregate can itself serve as a binary C ABI library.

## Active execution order

Do one phase at a time. Completing a phase means meeting its stated acceptance criteria, updating
this plan and the relevant documentation, running the required tests, and committing that focused
task. Do not start a later broad API phase merely because an earlier skeleton compiles.

| Phase | Purpose | Entry condition |
|---|---|---|
| B0 | Design and compatibility contract | Owner authorizes C ABI implementation planning to proceed |
| B1 | Build/module/export foundation | B0 design gate accepted |
| B2 | Common ABI substrate | B1 pure-C header gate green |
| B3 | Runtime/game callback vertical slice | B2 handle/error contracts green |
| B4 | Minimal usable 2D graphics and input | B3 real C loop green |
| B5 | Content and expanded input/audio | B4 ownership and renderer matrix green |
| B6 | Advanced graphics and explicit streams/async | A concrete C use case authorizes each API family |
| B7 | Hardening, packaging and experimental release | B3–B6 selected scope is complete |

## Planning baseline

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-000 | Record the C ABI implementation plan | ✅ | `plan_binding.md`, `NEXT.md`, `AUDIT.md` and `AGENTS.md` identify the C-only scope, all planned phases and the two read-only analysis sources. No implementation or ABI commitment is made. |

## Phase B0 — design and compatibility contract

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-001 | Freeze the C ABI charter | ⬜ | Add `docs/c-api/README.md` stating scope, experimental status, C++-canonical ownership, no language-specific binding scope, supported platform policy, and the non-negotiable invariants above. |
| CBIND-002 | Define ABI naming, export and version policy | ⬜ | Specify `CNA_*` types, `cna_*` functions, a platform export macro, ABI semantic-version encoding/query, experimental/stable tiers, deprecation rules, and a no-breaking-change-within-major policy. |
| CBIND-003 | Define primitive and POD layout policy | ⬜ | Select fixed-width integer, float, boolean, enum and length/count representations; define struct alignment/initialization rules, `struct_size`/`struct_version` use, nullability, overflow conversion and C standard baseline. Do not use `size_t` as an ABI field or parameter unless the owner explicitly accepts its platform dependence. |
| CBIND-004 | Define handles and ownership model | ⬜ | Specify the opaque-handle encoding, slot/generation validation, runtime type checks, retain/release policy, borrowed-callback validity, parent/child lifetime, thread-affine release policy and teardown behavior. Include stale/double-release outcomes. |
| CBIND-005 | Define error, UTF-8, buffer and collection contracts | ⬜ | Specify complete `CNA_Result` and error-category sets; thread-local/error-object retrieval and invalidation; UTF-8 validation and embedded-NUL policy; caller-buffer query/copy convention; pointer-plus-count bulk transfers; capacity/written semantics; and overflow/error behavior. |
| CBIND-006 | Define callback, threading and re-entrancy contract | ⬜ | Specify game callback table shape, callback result propagation, callback context lifetime, registration/unregistration, allowed re-entry, main/graphics/audio thread requirements, cross-thread calls and shutdown order. |

**B0 gate:** the six documents form one reviewed contract. No public C header or exported function is
added before their decisions are consistent with each other and with current CNA renderer behavior.

## Phase B1 — module, build and export foundation

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-007 | Add opt-in physical C API module | ⬜ | Add `modules/c-api/` to the physical-module composition and source-partition validator behind a clearly documented CMake option. It links canonical CNA module targets rather than duplicating source. Its option defaults and supported renderer configurations are deliberate and tested. |
| CBIND-008 | Enable a real C consumer build path | ⬜ | Enable the C language only when the C API/test option needs it, set the documented minimum C standard, and compile a standalone `.c` fixture through the normal build. Existing C++-only configurations stay unaffected when the option is off. |
| CBIND-009 | Produce a consumable native library | ⬜ | Define the shared-library-first and static-library policy, target aliases, install/export rules, transitive native dependencies and position-independent-code requirements. A C compiler can link and run the shared-library smoke executable without depending on C++ headers. |
| CBIND-010 | Establish visibility and symbol discipline | ⬜ | Implement cross-platform import/export declarations; verify that documented `cna_*` symbols are exported and unintended C++/Sharp Runtime implementation symbols are not part of the declared ABI. |
| CBIND-011 | Establish public-header quality gates | ⬜ | Add C17 (or the B0-selected standard) compile tests with strict warnings; reject C++ tokens/Sharp Runtime leakage; compile each leaf header alone and the umbrella header under C and C++. |

**B1 gate:** a minimal `cna_get_abi_version()`/capability query can be included, compiled from C,
linked to the intended library form and run on a supported native configuration. It must not expose
any CNA C++ object.

## Phase B2 — common ABI substrate

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-012 | Implement result and structured-error boundary | ⬜ | Every prototype entry point is wrapped by an exception firewall that maps known native failures and catch-all failures to `CNA_Result` plus documented error information. Error text is copied through the B0 buffer contract and is isolated per documented thread/context scope. |
| CBIND-013 | Implement validated handle registry | ⬜ | Create/destroy/lookup validates null, kind, generation and shutdown state. Tests prove invalid, stale, cross-kind and double-release calls fail safely and cannot alias a later object. |
| CBIND-014 | Implement neutral value and string conversion | ⬜ | Add only approved POD values (including `CNA_GameTime`, geometry/math and color representations needed by the first vertical slice) plus UTF-8 conversion. Test byte layout and semantic conversion independently; never reinterpret a C struct as a C++ XNA object. |
| CBIND-015 | Implement buffer/count-copy helpers | ⬜ | Provide reusable checked conversion and query/copy helpers for fixed-width counts, pointer ranges and destination capacity. Test zero length, null-with-zero, null-with-nonzero, undersized capacity, oversized values and all integer-overflow paths. |
| CBIND-016 | Audit the Sharp Runtime boundary | ⬜ | Produce `docs/c-api/SHARP_RUNTIME_BOUNDARY.md` with a C++/Sharp Runtime → C ABI mapping table for strings, exceptions, collections, spans, time, streams, tasks and delegates. Add a header scanner plus C compiler gate that rejects forbidden surface tokens. |

**B2 gate:** all common contracts have focused C and C++ tests, and sanitizers find no invalid
handle, conversion, ownership or exception-escape defect in the exercised paths.

## Phase B3 — runtime and game-callback vertical slice

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-017 | Design and implement runtime/game creation | ⬜ | Introduce versioned creation/configuration structs and a runtime/game handle model that maps to CNA's actual initialization and compile-time renderer selection. It must report unsupported configuration rather than inventing runtime renderer switching. |
| CBIND-018 | Implement lifecycle callback bridge | ⬜ | Expose only C callback tables plus context pointers for the approved load/update/draw/unload/exit lifecycle. Borrowed handles passed to callbacks have documented lifetime; callback failures, re-entrancy and exit requests follow B0 exactly. |
| CBIND-019 | Expose frame timing, clear and window-title minimum | ⬜ | Map `GameTime`, deterministic frame processing/run policy, exit, clear and UTF-8 window title through the C contract. No internal `Game`, `GameWindow`, `System::String` or renderer pointer is exposed. |
| CBIND-020 | Add C-only headless lifecycle test | ⬜ | A standalone C program creates a deterministic game, receives callbacks, clears a frame, requests exit and tears down under the HEADLESS renderer. It asserts callback order, values, error behavior and no leaked handles. |
| CBIND-021 | Add native-renderer lifecycle smoke test | ⬜ | Run the same C source under one supported windowed renderer/configuration when the environment permits it. Record renderer-specific skips and never claim pixel validation from a headless-only run. |

**B3 gate:** a C application can own its lifecycle, receive callbacks, exercise UTF-8 and error
conversion, and shut down cleanly without any C++ source or header dependency.

## Phase B4 — minimal usable 2D graphics and input

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-022 | Expose borrowed graphics-device access and capability discovery | ⬜ | Define the callback-scoped graphics-device handle, active renderer identity, device capabilities and not-supported behavior. Renderer names/capabilities are queried from CNA rather than duplicated in the C API. |
| CBIND-023 | Expose `Texture2D` ownership and bulk upload | ⬜ | Add create, dimensions, format subset, bulk `SetData`/readback where CNA supports it, and release semantics. Validate source spans and map failures through `CNA_Result`; test texture lifetime through normal CNA teardown. |
| CBIND-024 | Expose a batched `SpriteBatch` command path | ⬜ | Add begin/submit-many/end semantics with a POD command array, not one ABI transition per sprite. Define texture reference/lifetime during a batch, sort/state limits and behavior for unsupported renderer features. |
| CBIND-025 | Expose input as snapshots | ⬜ | Add keyboard (then mouse/game-pad only when each is specified) snapshots and query helpers with explicit frame/thread semantics. Do not expose live internal input classes or per-key callbacks. |
| CBIND-026 | Validate 2D results through C | ⬜ | Add a C test that creates/uploads a texture and emits a deterministic sprite batch. Use HEADLESS for lifecycle/state assertions and a supported real renderer for pixel assertions where available. |
| CBIND-027 | Document the initial C API feature matrix | ⬜ | Publish exact supported operations, renderer limits, error behavior, resource ownership and intentionally unavailable XNA/C++ features. Do not label the surface as full XNA coverage. |

**B4 gate:** a pure C 2D application can create a game, upload a texture, submit a batched draw,
read an input snapshot and release all owned resources under at least one real renderer plus the
HEADLESS deterministic control.

## Phase B5 — content, expanded input and audio

Each row begins only after a concrete C application needs it. APIs remain compact and semantic;
they do not export C++ collections or attempt to mirror C++ overload sets mechanically.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-028 | Expose `ContentManager` minimum | ⬜ | Add content-manager ownership, UTF-8 root directory and approved resource-load functions. Define cache ownership and resource release behavior precisely; native path/stream objects remain hidden. |
| CBIND-029 | Expose expanded input snapshots | ⬜ | Add mouse, game-pad and touch snapshots only after their layouts, dead-zone/normalization behavior and platform availability are specified and tested in C. |
| CBIND-030 | Expose minimal audio resource/control surface | ⬜ | Add only concrete C use-case APIs (for example sound creation/play/stop/volume) with explicit audio-thread and deferred-destruction behavior. No Sharp Runtime collection or async object crosses the boundary. |
| CBIND-031 | Add pure-C content/audio regression programs | ⬜ | Verify UTF-8 paths, predictable load failures, handle ownership, audio shutdown and unavailable-device behavior using fixtures that do not rely on a future language binding. |
| CBIND-032 | Extend capability reporting | ⬜ | Report feature/platform availability through stable C APIs so a C application can degrade gracefully instead of hard-coding build or renderer assumptions. |

## Phase B6 — advanced API families, only when justified

No row in this phase is a promise to export the entire CNA surface. Each begins with a small
design review that confirms it has a C use case, a clear resource/ownership model, a renderer
support policy and a pure-C test strategy.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-033 | Add render targets, sprite fonts and graphics state as needed | ⬜ | Expose compact handles/POD descriptors and capability/error behavior; verify resource ownership and renderer-specific refusal paths from C. |
| CBIND-034 | Add 3D resources and draw submission as needed | ⬜ | Design C-native vertex/index data layouts, effects/model/state handles and bulk submissions. Require real-renderer correctness tests; do not claim all renderer parity from structural tests. |
| CBIND-035 | Add stream callbacks only for a demonstrated native-reads-foreign-data need | ⬜ | Define read/seek/length/close callbacks, context ownership, callback threading, cancellation and error conversion. Never expose `System::IO::Stream` or a C++ stream pointer. |
| CBIND-036 | Add neutral asynchronous-operation APIs only for an inherently asynchronous CNA feature | ⬜ | Define operation handles, poll/cancel/wait/callback semantics, result retrieval and shutdown. Never expose `Task`, `std::future` or Sharp Runtime task objects. |
| CBIND-037 | Add collection/enumeration APIs only as count/copy or stable-handle operations | ⬜ | Prohibit public container layouts. Test mutation during enumeration, insufficient capacity, ownership and thread rules. |

## Phase B7 — hardening, documentation and experimental release

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-038 | Expand pure-C compatibility matrix | ⬜ | Build public headers and C smoke programs with the selected C compilers and target platforms; run selected renderer/headless configurations and record supported/skipped combinations honestly. |
| CBIND-039 | Add ABI layout, export and compatibility gates | ⬜ | Check struct size/offset/alignment, enum numeric values, ABI version behavior, exported-symbol allowlist and a baseline compatibility snapshot. New ABI fields/functions follow B0 policy. |
| CBIND-040 | Add safety and lifetime stress tests | ⬜ | Run invalid-handle, double-release, stale-generation, shutdown-order, callback-unregister, UTF-8/buffer-boundary and high-volume create/release tests under ASan/UBSan where supported. Add focused fuzz targets for parser-like/buffer-facing APIs. |
| CBIND-041 | Publish C consumer documentation and examples | ⬜ | Document CMake consumption, shared/static linking, initialization, error retrieval, UTF-8, buffers, handles, callbacks, threading, renderer limits and clean shutdown. Every example is C-only and builds in CI. |
| CBIND-042 | Define experimental release gate | ⬜ | Require the B7 matrix, a real C application, documentation, installability, no unreviewed ABI break and a known-limitations matrix before publishing an experimental C ABI release. ABI 1.0 requires a later explicit release decision. |

## Mandatory test layers

Every implemented public C entry point must receive all applicable coverage in the same task:

1. **Pure C compile test:** includes the leaf and umbrella headers in the selected C standard;
   proves no C++/Sharp Runtime leakage.
2. **C link/runtime test:** a C translation unit calls the shared C API through the documented
   library target, not a private C++ test helper.
3. **Native adapter test:** validates C-to-C++ semantic conversion, exception firewall and cleanup
   behavior with the existing CNA test framework.
4. **Negative/lifetime test:** validates null/invalid/stale/wrong-kind handles, buffers, UTF-8,
   bad enums, double release and shutdown ordering as applicable.
5. **Renderer test:** HEADLESS supplies deterministic lifecycle/state control; any draw claim
   additionally needs a real supported renderer and a renderer-appropriate observable result.
6. **ABI regression test:** protects externally visible sizes, offsets, numeric constants, symbols
   and ABI-version compatibility against accidental changes.

## Per-API admission checklist

Before adding an API family or function, answer and document all of the following:

1. What concrete C use case requires it?
2. Which canonical CNA C++ operation defines its semantics and FNA behavior where it is XNA-facing?
3. Does the signature contain only C-safe fixed-width/POD/handle/callback constructs?
4. How do ownership, nullability, borrowed lifetime, parent lifetime and thread affinity work?
5. How are strings, collections, buffers, paths, streams, callbacks and errors represented?
6. Can all exceptions be caught and mapped without leaking implementation type names?
7. What happens for unsupported renderer/platform capability?
8. What C-only success, failure, lifetime and ABI tests will land with it?
9. Does it expose an internal Sharp Runtime, STL, C++ or renderer-private concept? If so, redesign it.
10. Is the ABI extension additive within its current ABI major? If not, stop for an explicit versioning decision.

## Completion criteria for the initial C ABI milestone

The initial milestone is complete only when all selected B0–B4 rows are ✅ and the following
statement is demonstrably true:

> A C program, compiled as C and including only `<CNA/C/cna.h>`, can create and run a small CNA
> game, receive lifecycle callbacks, clear a frame, upload a texture, submit a batched sprite draw,
> read a documented input snapshot, retrieve a UTF-8 error on failure, and release every owned
> resource without leaking or using a C++/Sharp Runtime ABI type.

This is an **experimental C ABI milestone**, not a promise of XNA-wide C coverage, ABI 1.0, or a
future language-specific binding.

## Current status

All tasks `CBIND-001` through `CBIND-042` are ⬜ **not started**. This planning task added no C
headers, C/C++ source, CMake target, generated code, language-specific binding, package, test
binary or ABI commitment. The next action requires the owner's explicit authorization to begin
Phase B0.
