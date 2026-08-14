# CNA Native C Binding / Stable C ABI — Implementation Plan

> **Status: IMPLEMENTATION AUTHORIZED — B0–B5 complete; B6 complete through CBIND-034 under HEADLESS and SDL_RENDERER (2026-08-14).** This document is
> the plan for a native C API, implemented inside the main CNA repository. It is intentionally
> not a plan for C#, .NET, JavaScript/TypeScript, Rust, Python, Java, Zig, Go, Swift, or any other
> language-specific binding. Such work must not begin, nor be planned here, without a new explicit
> owner instruction.

> **Authoritative design inputs (read-only):** `analysis_binding.md` and
> `analysis_binding_sharp_runtime.md`. The behavioral reference for the underlying XNA-facing C++
> implementation remains the local FNA tree required by `AGENTS.md`.

## Goal

Expose a documented, testable and eventually complete **C ABI** over canonical CNA C++.

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
- a documented C-native equivalent for every public CNA C++ type, member, overload, constant and
  event, using handles, POD values and callbacks where C cannot express the original C++ form;
- C applications that link the native CNA library and use only public C headers;
- the C API's own lifecycle, graphics, input, content, audio, data-transfer and callback contracts;
- C-only compile/link/runtime tests, native adapter tests, documentation, export inspection and
  supported-platform packaging.

Out of scope:

- a language-specific binding, wrapper, package, generator or sample for any language other than C;
- a separate C engine or a second implementation of CNA;
- exporting arbitrary C++ classes, Sharp Runtime, STL, renderer-private or platform-private objects;
- declaring ABI 1.0 before experimental releases are exercised by real C applications.

"Complete" means behavioral and conceptual coverage of the public CNA API, not a false claim that
C can use C++ inheritance, templates, exceptions, overload resolution or Sharp Runtime object
layouts directly. Every such member must instead have a documented C mapping or a documented,
testable native limitation in the C API coverage matrix; omissions are tracked as incomplete.

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
└── COVERAGE.md
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
| B6 | Full public CNA API coverage | B5 foundation and the coverage inventory are green |
| B7 | Hardening, packaging and experimental release | B3–B6 selected scope is complete |

## Planning baseline

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-000 | Record the C ABI implementation plan | ✅ | `plan_binding.md`, `NEXT.md`, `AUDIT.md` and `AGENTS.md` identify the C-only scope, all planned phases and the two read-only analysis sources. No implementation or ABI commitment is made. |

## Phase B0 — design and compatibility contract

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-001 | Freeze the C ABI charter | ✅ | `docs/c-api/README.md` states scope, experimental status, C++-canonical ownership, complete-public-API coverage requirement, no language-specific binding scope, supported platform policy, and the non-negotiable invariants above. |
| CBIND-002 | Define ABI naming, export and version policy | ✅ | `docs/c-api/ABI_VERSIONING.md` specifies `CNA_*` types, `cna_*` functions, a platform export macro, ABI semantic-version encoding/query, experimental/stable tiers, deprecation rules, and a no-breaking-change-within-major policy. |
| CBIND-003 | Define primitive and POD layout policy | ✅ | `docs/c-api/ABI_VERSIONING.md` and `docs/c-api/STRINGS_AND_BUFFERS.md` select fixed-width integer, float, boolean, enum and length/count representations; define struct alignment/initialization rules, `struct_size`/`struct_version` use, nullability, overflow conversion and C17 baseline. They prohibit `size_t` in ABI fields/parameters. |
| CBIND-004 | Define handles and ownership model | ✅ | `docs/c-api/HANDLES.md` and `docs/c-api/OWNERSHIP.md` specify opaque-handle encoding, slot/generation validation, runtime type checks, retain/release policy, borrowed-callback validity, parent/child lifetime, thread-affine release policy and teardown behavior. |
| CBIND-005 | Define error, UTF-8, buffer and collection contracts | ✅ | `docs/c-api/ERRORS.md` and `docs/c-api/STRINGS_AND_BUFFERS.md` specify `CNA_Result`, error categories, per-thread error retrieval, UTF-8 validation, caller-buffer query/copy semantics, pointer/count bulk transfers, capacity/written semantics and overflow behavior. |
| CBIND-006 | Define callback, threading and re-entrancy contract | ✅ | `docs/c-api/CALLBACKS_AND_THREADING.md` specifies callback result propagation, context lifetime, registration/unregistration, permitted re-entry, thread requirements, cross-thread calls and shutdown order. |

**B0 gate:** the six documents form one reviewed contract. No public C header or exported function is
added before their decisions are consistent with each other and with current CNA renderer behavior.

## Phase B1 — module, build and export foundation

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-007 | Add opt-in physical C API module | ✅ | `modules/c-api/` is an opt-in physical module included in the source-partition validator. Its initial shared library links only canonical `cna_core`; each later C API family must add the exact CNA module it adapts rather than prematurely linking the renderer aggregate. |
| CBIND-008 | Enable a real C consumer build path | ✅ | `CNA_BUILD_C_API=ON` enables C17 before dependencies/modules are created. C17 smoke executables compile and link through the normal CMake build without changing C++-only configurations when the option is off. |
| CBIND-009 | Produce a consumable native library | ✅ | `cna_c_api` / `CNA::CApi` builds as `libcna_c_api` with CMake install/export rules, public include installation and PIC enabled before static dependencies are created. A C compiler links and runs smoke executables against the shared library. |
| CBIND-010 | Establish visibility and symbol discipline | ✅ | `CNA_C_API` supplies platform export/import declarations; ELF C++ visibility is hidden by default. The HEADLESS build's dynamic export inspection contains only the documented `cna_get_abi_version` and error-query symbols. |
| CBIND-011 | Establish public-header quality gates | ✅ | C17 and C++23 object targets compile both leaf headers and the umbrella header under strict direct compiler checks; CTest smoke consumers include only `CNA/C/cna.h`. |

**B1 gate:** a minimal `cna_get_abi_version()`/capability query can be included, compiled from C,
linked to the intended library form and run on a supported native configuration. It must not expose
any CNA C++ object.

## Phase B2 — common ABI substrate

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-012 | Implement result and structured-error boundary | ✅ | The thread-local versioned error/query/copy substrate is backed by a reusable exception firewall. It maps allocation, argument/range, I/O, standard and unknown C++ failures to stable results/categories without exposing exception objects; focused tests verify mapping and diagnostic copying. |
| CBIND-013 | Implement validated handle registry | ✅ | Slot/generation/kind/thread-affinity validation now backs the public owned `CNA_Game` handle as well as focused stale/double-release/reuse tests. The one-active-game state is released only after callback/native teardown; wrong-thread and stale public calls fail safely. |
| CBIND-014 | Implement neutral value and string conversion | ✅ | UTF-8 string-view validation/copy covers nullability, overlong encodings and optional embedded-NUL rejection. The first vertical slice adds independently laid out/tested `CNA_GameTime` and `CNA_Color` POD values; no C POD is reinterpreted as a C++ object. |
| CBIND-015 | Implement buffer/count-copy helpers | ✅ | Reusable pointer/count and element-size helpers validate null/zero cases, checked `uint64_t` multiplication and native-size conversion. Focused tests cover zero/null, nonzero-null and overflow; error-copy tests cover undersized capacity with no partial write. |
| CBIND-016 | Audit the Sharp Runtime boundary | ✅ | `docs/c-api/SHARP_RUNTIME_BOUNDARY.md` records the mapping table. A CMake lexical scanner and strict C17/C++23 compiler gates audit each public header; the pure-C umbrella consumer remains the authoritative boundary test. |

**B2 gate:** all common contracts have focused C and C++ tests, and sanitizers find no invalid
handle, conversion, ownership or exception-escape defect in the exercised paths.

## Phase B3 — runtime and game-callback vertical slice

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-017 | Design and implement runtime/game creation | ✅ | Versioned `CNA_GameCreateInfo`/`CNA_GameCallbacks` create one owned, generation-checked C game over the canonical CNA `Game`. The compile-time renderer remains CNA-owned; no runtime renderer switch is invented. |
| CBIND-018 | Implement lifecycle callback bridge | ✅ | The copied C callback table covers load/update/draw/unload/exit with a caller context, callback-scoped game handle, `CNA_GameTime` where applicable and copied versioned callback diagnostics. Failure stops the loop and reports `CNA_RESULT_CALLBACK`; run/destroy re-entry is refused. |
| CBIND-019 | Expose frame timing, clear and window-title minimum | ✅ | `CNA_GameTime`, one-frame/blocking run, exit request, `CNA_Color` clear and UTF-8 title functions adapt canonical `Game`, `GraphicsDevice` and `GameWindow` operations without exposing their C++ types. |
| CBIND-020 | Add C-only headless lifecycle test | ✅ | `LifecycleSmoke.c` creates, drives, clears, exits and destroys C games under HEADLESS; it tests callback order/values, callback diagnostics, stale handles, wrong-thread rejection and a blocking run path. |
| CBIND-021 | Add native-renderer lifecycle smoke test | ✅ | The same strict-C lifecycle source builds and passes against `SDL_RENDERER` with SDL's dummy video driver and software renderer. The earlier `ranlib` failure was traced to overlapping verification builds rather than a CNA archive defect; a single clean serial build completed through `cna_c_api_lifecycle_smoke`. |

**B3 gate:** a C application can own its lifecycle, receive callbacks, exercise UTF-8 and error
conversion, and shut down cleanly without any C++ source or header dependency.

## Phase B4 — minimal usable 2D graphics and input

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-022 | Expose borrowed graphics-device access and capability discovery | ✅ | `graphics.h` defines callback-scoped device borrowing, stable identities for every canonical `GraphicsRendererType`, versioned renderer info, UTF-8 renderer-name count/copy and the complete canonical `GraphicsCapability` query/bit set. Callback return generation-invalidates the borrowed handle; identity, maximum texture size and support answers delegate to CNA rather than a duplicate renderer feature table. |
| CBIND-023 | Expose `Texture2D` ownership and bulk upload | ✅ | All canonical `SurfaceFormat` identities are frozen; the initial supported Color subset provides versioned create/info, full-level bulk RGBA8 `SetData`/readback and explicit dispose/release. Pointer/count, dimensions, capacity, stale/double-destroy and parent-before-child errors are C-tested under HEADLESS and SDL_RENDERER; game destruction refuses live C graphics children. |
| CBIND-024 | Expose a batched `SpriteBatch` command path | ✅ | All five native sort identities and both effect bits are frozen; an owned same-game SpriteBatch accepts a fully prevalidated, versioned POD command array through one C ABI call. The initial state set is explicitly fixed to XNA defaults, textures are retained through successful `End`, active destruction cancels safely, and native `NotSupportedException` maps to `CNA_RESULT_NOT_SUPPORTED`; HEADLESS and SDL_RENDERER C tests cover state, validation, lifetime and stale handles. |
| CBIND-025 | Expose input as snapshots | ✅ | `input.h` freezes all 160 canonical `Keys` identities and captures a fresh canonical 256-key `KeyboardState` POD per call on the active game's creation thread. Key tests and ascending count/copy are runtime-free POD helpers valid on any thread; full-array, invalid-key, no-partial-copy and wrong-thread behavior is C-tested under HEADLESS and SDL_RENDERER. No live input object, per-key native call or callback crosses the ABI; mouse/game-pad/touch remain in the already planned expanded-input task. |
| CBIND-026 | Validate 2D results through C | ✅ | The strict-C lifecycle program creates and uploads a 2×2 RGBA texture, submits deterministic SpriteBatch commands and uses a versioned logical-backbuffer descriptor plus full RGBA8 count/copy readback. HEADLESS proves `CNA_RESULT_NOT_SUPPORTED` with untouched output; SDL_RENDERER proves exact red/green/blue texture pixels and an untouched clear pixel before presentation. |
| CBIND-027 | Document the initial C API feature matrix | ✅ | `docs/c-api/FEATURE_MATRIX.md` publishes the exact experimental 0.1 function families, HEADLESS/SDL_RENDERER evidence, ownership/thread/capacity/error behavior and explicit unavailable families. It distinguishes enumerated renderer identities from tested support and repeatedly states that the slice is not complete CNA/XNA coverage. |

**B4 gate:** a pure C 2D application can create a game, upload a texture, submit a batched draw,
read an input snapshot and release all owned resources under at least one real renderer plus the
HEADLESS deterministic control.

## Phase B5 — content, expanded input and audio

Each row begins only after a concrete C application needs it. APIs remain compact and semantic;
they do not export C++ collections or attempt to mirror C++ overload sets mechanically.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-028 | Expose `ContentManager` minimum | ✅ | `content.h` owns a game-child native manager created from the callback-scoped device, copies/counts its UTF-8 root, exposes explicit cache unload/destroy and provides the first approved typed load for Color Texture2D. Every successful load returns a new independently owned existing C texture handle that survives manager unload/destruction; missing assets and invalid names map predictably, and no native path, stream, service provider or template type crosses the ABI. HEADLESS and SDL_RENDERER strict-C tests cover pixels, cache/unload lifetime, parent order, stale handles and thread/UTF-8/capacity failures. |
| CBIND-029 | Expose expanded input snapshots | ✅ | `input.h` now freezes fixed-layout mouse, four-player gamepad and eight-location touch snapshots. Capture is fresh and creation-thread-bound; disconnected devices return successful rest/empty values. Both native GamePad state overloads, all three dead-zone modes and all 31 current button bits are mapped, with exact pure-POD normalization/button helpers. Touch capability/state includes previous locations and CNA pressure plus local FindById/TryGetPrevious behavior. Strict-C HEADLESS and SDL_RENDERER tests cover all player/mode paths, synthetic numeric edge cases, absence, invalid inputs and wrong-thread refusal; ABI layout tests freeze every new POD. |
| CBIND-030 | Expose minimal audio resource/control surface | ✅ | `audio.h` maps canonical channel/state identities and a concrete owned PCM16LE `SoundEffect` → controllable `SoundEffectInstance` route: duration, play/pause/resume, immediate/release-tail stop, volume/pitch/pan/loop/info and explicit destruction. Bytes are copied; instance-before-effect-before-game ordering is enforced; all public calls are creation-thread-bound while the internal mixer keeps no C callback/context. No-device creation maps to `NOT_SUPPORTED`, native track disposal defines return-time handle invalidation, and strict-C dummy-audio tests freeze layout, validation, transitions, stale handles, parent order and wrong-thread refusal. |
| CBIND-031 | Add pure-C content/audio regression programs | ✅ | `ContentSmoke.c` now loads its exact pixel fixture through a real valid non-ASCII UTF-8 filename while retaining malformed/embedded-NUL, missing-file IO, cache and ownership coverage. `AudioSmoke.c` covers the successful dummy-device lifecycle, and isolated `AudioUnavailableSmoke.c` forces a nonexistent SDL driver twice to prove stable `NOT_SUPPORTED`, invalid output handles, structured diagnostics, no leaked child count and clean game shutdown. The same strict-C programs pass with HEADLESS and SDL_RENDERER and depend on no future language binding. |
| CBIND-032 | Extend capability reporting | ✅ | Graphics renderer identity plus all 13 native graphics capabilities, touch connection/count and now native audio playback availability have stable versioned C query routes. `cna_audio_get_capabilities` probes CNA's real process-wide mixer, returns `SUCCESS` plus false when no device can open, creates no owned C resource, and preserves argument/handle/thread failures. Strict-C dummy and deliberately invalid audio drivers prove both outcomes under HEADLESS and SDL_RENDERER; fixed layout and zeroed reserves are ABI-tested. |

## Phase B6 — complete public CNA API coverage

This phase is the commitment to complete coverage of the public CNA surface. Each API family still
needs a C-native design review: complete coverage never permits a raw C++ ABI leak or an untested
mechanical wrapper.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-033 | Inventory the complete public CNA surface | ✅ | Doxygen-backed `tools/c-api/generate_coverage_inventory.py` deterministically tracks all 414 public headers and 6,415 public/protected declarations while explicitly excluding 95 `Internal`/`Detail` headers and the C API itself. `coverage_mappings.json` assigns reviewed current mappings; every remaining symbol has a C-native mapping proposal, test obligation, status and owner task. The snapshot records 443 implemented, 21 partial, 5,881 planned and 70 explicitly deleted/not-applicable declarations; `--check` proves drift without prematurely wiring the CBIND-043 CI gate. |
| CBIND-034 | Add render targets, sprite fonts and graphics state coverage | ✅ | `graphics_state.h`, `display.h`, `render_target.h` and `sprite_font.h` map every inventory row in this family: fixed identities and complete state PODs/presets/device round-trips, sampler slots and explicit-state SpriteBatch Begin; display/adapter/presentation values and safe native-handle/refresh limitations; owned 2D/cube targets with applied-property snapshots and atomic singular/MRT binding; copied glyph SpriteFonts retaining their source Texture2D. Strict-C HEADLESS and SDL_RENDERER tests cover ABI layouts, properties, UTF-8, ownership, stale/wrong-thread handles, real 2D binding and honest unavailable-backend paths. The inventory now records 814 implemented, 21 partial, 5,510 planned and 70 not applicable rows, with no planned CBIND-034 row. |
| CBIND-035 | Add 3D resources, effects, models and draw-submission coverage | 🟨 | Design C-native vertex/index data layouts, effects/model/state handles and bulk submissions for all public APIs in these families. Require real-renderer correctness tests; do not claim all renderer parity from structural tests. Work is decomposed into CBIND-035A–G below; the parent becomes complete only when all seven rows and every CBIND-035 inventory row are closed. |
| CBIND-036 | Add stream, storage, networking and asynchronous-operation coverage | ⬜ | Define stream callbacks, storage/network objects and neutral operation handles where the canonical API needs them, with documented ownership, thread, cancellation and error conversion. Never expose `System::IO::Stream`, `Task`, `std::future` or a C++ pointer. |
| CBIND-037 | Add collections, events, services, media and devices coverage | ⬜ | Map every public collection/event/service/media/device API to count/copy, stable-handle or callback forms. Prohibit public container layouts and test mutation, capacity, ownership and thread rules. |
| CBIND-043 | Maintain a machine-checked coverage gate | ⬜ | A CI checker compares the public-header inventory to `COVERAGE.md` and fails if a public type/member/constant/event has no mapping/status. New C++ public API cannot land without its C API row and tests in the same change. |
| CBIND-044 | Close the public API coverage matrix | ⬜ | Every row is implemented and tested, or carries an owner-approved native limitation with a callable C API that reports it. No unspecified omission remains. |

### CBIND-035 implementation slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035A | Establish 3D value and identity ABI | ✅ | `math_values.h` now defines fixed-layout Point, Vector4, Quaternion, Matrix, Plane, Ray and bounding-volume PODs, all 17 public PackedVector raw-storage aliases and stable containment/plane/curve identities. `graphics3d.h` freezes buffer/index/primitive/SetData/vertex identities and the four-field `CNA_VertexElement`. Strict C17 and C++23 assertions cover every represented storage width, representative/full field offsets and identity ordinals under HEADLESS and SDL_RENDERER. Coverage maps only the 169 directly represented type/field/property/identity rows; all constructors, constants and operations remain owned by CBIND-035B. |
| CBIND-035B | Complete math, geometry and packed-value operations | 🟨 | Map every remaining public math and PackedVector constructor, method, operator, constant and collection behavior to C-native value/bulk operations with numeric edge-case tests. Work is decomposed into CBIND-035B1–B7 below; this row becomes complete only after all seven slices and every corresponding inventory row are closed. |
| CBIND-035C | Add texture, buffer and vertex-resource coverage | ⬜ | Complete Texture/Texture2D/3D/Cube transfer variants, vertex/index buffers, declarations/bindings and resource lifetime through validated handles and bulk byte/value operations. |
| CBIND-035D | Add effects, shaders and parameter coverage | ⬜ | Map Effect/technique/pass/parameter/annotation collections, stock/custom effects and shader/material extensions without exposing bytecode objects, C++ containers or backend pointers. |
| CBIND-035E | Add model, mesh and animation coverage | ⬜ | Map model/bone/mesh/part collections, animation and morph/skinning/material extensions through stable handles, count/copy and bulk transforms. |
| CBIND-035F | Complete graphics-device and draw submission | ⬜ | Map remaining device properties/events/clear/present/draw overloads, viewport/scissor, texture collections and SpriteBatch transform/effect/text routes using validated descriptors and bulk submissions. |
| CBIND-035G | Close and verify CBIND-035 | ⬜ | No planned CBIND-035 inventory row remains; strict C tests cover HEADLESS refusal plus actual 3D/effect/model output on suitable real renderers, with capability gaps recorded honestly. |

#### CBIND-035B math implementation slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035B1 | Complete Point and Rectangle operations | ✅ | `math.h` maps both complete source headers through 37 exported operations covering constructors, named zero/empty values, every property/overload/operator, hashes and exact UTF-8 count/copy strings. Unsigned-bit arithmetic preserves C# unchecked 32-bit wraparound without C++ signed-overflow UB; division rejects zero and the unrepresentable minimum/-1 quotient without partial output. `MathValuesSmoke.c` calls every entry point and covers boundaries, mutation, half-open containment, intersection/union, capacity and null failures under HEADLESS, SDL_RENDERER and ASan+UBSan. |
| CBIND-035B2 | Complete MathHelper and Vector2/3/4 operations | ✅ | `math.h` and `vectors.h` map every MathHelper and Vector2/3/4 public inventory row through exact constants and fallible scalar/value/bulk operations. All overload-equivalent, finite/non-finite, exact-string, null/range-atomicity and sequential-aliasing contracts are covered in strict C under HEADLESS, SDL_RENDERER and ASan+UBSan. Completed as CBIND-035B2a–B2d. |
| CBIND-035B3 | Complete Quaternion and Matrix operations | ✅ | `quaternion.h` and `matrix.h` map every remaining public row through 85 fallible operations. Both constructors/constants/properties and all member/static/operator math, decomposition/interpolation/transformation/factory routes are covered with row-major, singular, projection-failure, non-finite and aliasing evidence under both backends and ASan+UBSan. Completed as CBIND-035B3a–B3b. |
| CBIND-035B4 | Complete planes, rays and bounding-volume operations | ✅ | `geometry.h` maps every remaining Plane, Ray, BoundingBox, BoundingSphere and BoundingFrustum row through C-native values, explicit optional hits and caller-capacity corners. Strict-C HEADLESS/SDL_RENDERER and ASan+UBSan tests cover every exported operation, including atomic capacity/failure paths and the canonical unsupported boundary-ray case. Completed as CBIND-035B4a–B4d. |
| CBIND-035B5 | Complete Curve value, collection and evaluation operations | ✅ | `curve.h` maps all 60 Curve, CurveKey and CurveKeyCollection rows through fixed values and validated handles without leaking C++ containers. Ordered collection mutation, retained mutable key views, all loop/evaluation/tangent behavior and lifetime/error boundaries are covered in strict C under both backends and ASan+UBSan. Completed as CBIND-035B5a–B5c. |
| CBIND-035B6 | Complete Color operations and named constants | ✅ | `color.h` and `named_colors.h` map the complete 175-row Color header through the four-byte POD, direct channels, 24 operations and 141 directly usable named value expressions. Every packed value is checked independently and all value/error behavior passes strict C under both backends and ASan+UBSan. Completed as CBIND-035B6a–B6b. |
| CBIND-035B7 | Complete PackedVector operations and close math coverage | ⬜ | Map all concrete packed constructors, float/vector conversions, equality/string/hash behavior and interface-equivalent storage routes; leave no planned math/PackedVector CBIND-035 row. |

##### CBIND-035B2 scalar/vector slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035B2a | Complete MathHelper | ✅ | Eight exact `CNA_MATH_*` constants and 15 fallible `cna_math_*` operations map every non-deleted public MathHelper row. The implementation delegates canonical interpolation/clamp/distance/angle/epsilon behavior and replaces the native signed-overflow-prone MSAA bit trick with a defined full-positive-int32 equivalent; negative nonsensical sample counts fail without output mutation. Strict-C tests call every operation across endpoints, NaN/infinity, epsilon, full-range MSAA and null/failure cases; C++23 freezes constant values. |
| CBIND-035B2b | Complete Vector2 | ✅ | `vectors.h` maps all 75 remaining Vector2 rows through 41 exported operations: three constructors, four constants, complete member/static/operator math, exact UTF-8 count/copy and single/bulk matrix, quaternion and normal transforms. Value/out-ref pairs intentionally share the C result-plus-output form; full/range vector overloads share a count/index/length descriptor with preflight validation and defined sequential aliasing. `VectorSmoke.c` calls every entry point and covers IEEE division, normalization, hashes/strings, overload-equivalent results, transforms, capacity/null/range atomicity under both backends and ASan+UBSan. |
| CBIND-035B2c | Complete Vector3 | ✅ | `vectors.h` maps all 87 remaining Vector3 rows through 50 exported operations: four constructors, eleven direction/value constants, complete member/static/operator math including cross products, exact UTF-8 count/copy and single/bulk matrix, quaternion and normal transforms. Value/out-ref pairs share the C result-plus-output form; full/range overloads use preflight-validated raw array ranges with defined sequential aliasing. `VectorSmoke.c` calls every entry point and covers IEEE division, normalization, hashes/strings, direction identities, transforms, capacity/null/range atomicity under both backends and ASan+UBSan. |
| CBIND-035B2d | Complete Vector4 | ✅ | `vectors.h` maps all 81 remaining Vector4 rows through 46 exported operations: five constructors, six constants, complete member/static/operator math, exact UTF-8 count/copy, typed Vector2/3/4 matrix and quaternion transforms, and validated Vector4 bulk ranges. Value/out-ref and full/range overloads collapse to the established output/range forms with defined IEEE and sequential-alias behavior. `VectorSmoke.c` calls every entry point under both backends and ASan+UBSan. This closes parent CBIND-035B2. |

##### CBIND-035B3 quaternion/matrix slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035B3a | Complete Quaternion | ✅ | `quaternion.h` maps all 50 remaining Quaternion rows through 28 exported operations: both constructors, identity, complete member/static/operator math, axis/matrix/yaw-pitch-roll factories, concatenation, inversion, Lerp/Slerp and exact UTF-8 count/copy. Value/out-ref overloads share the result-plus-output form. `QuaternionSmoke.c` calls every entry point and covers rotation identities, normalized interpolation, IEEE zero normalization, aliasing, exact strings and null/capacity failures under both backends and ASan+UBSan. |
| CBIND-035B3b | Complete Matrix and close parent B3 | ✅ | `matrix.h` maps all 98 remaining Matrix rows through 57 exported operations: both constructors, Identity, all seven directional/translation get/set properties, decomposition, determinant, equality/hash/string, every billboard/rotation/view/projection/scale/shadow/translation/reflection/world factory and complete arithmetic/transformation operators. Nullable pointers represent optional billboard directions. `MatrixSmoke.c` calls every entry point and covers row-major fields, property signs, decomposition success/failure outputs, projection rejection without output mutation, singular IEEE inversion and exact strings under both backends and ASan+UBSan. This closes parent CBIND-035B3. |

##### CBIND-035B4 geometry slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035B4a | Complete Plane and Ray | ✅ | `geometry.h` maps all 42 remaining Plane/Ray rows through 31 operations: all constructors, dot/normalization/transforms, volume classification, equality/hash/string and box/sphere/plane/frustum ray intersection. Native optional distances become an explicit hit flag plus distance, with zero distance on miss. `GeometrySmoke.c` calls every entry point and covers classifications, matrix/quaternion transforms, hit/miss distances, frustum containment, exact strings and null/capacity failures under both backends and ASan+UBSan. |
| CBIND-035B4b | Complete BoundingBox | ✅ | `geometry.h` maps all 31 remaining BoundingBox rows through one corner-count constant and 20 operations: construction, all containment/intersection overloads, factories, merge, equality/hash/string and explicit optional ray distance. Corner copy uses a caller-capacity array, always reports the required count and performs no partial write. `GeometrySmoke.c` calls every entry point and covers canonical corner order, capacity atomicity, classifications, hit/miss distances, factories, exact strings and null/empty failures under both backends and ASan+UBSan. |
| CBIND-035B4c | Complete BoundingSphere | ✅ | `geometry.h` maps all 31 remaining BoundingSphere rows through 21 operations: construction, nonuniform matrix transformation, every containment/intersection overload, box/frustum/point factories, merge, equality/hash/string and explicit optional ray distance. Point arrays use checked counts and preserve outputs on rejection. `GeometrySmoke.c` calls every entry point and covers containment boundaries, hit/miss distances, touching spheres, all factories, merge, exact strings and null/empty failures under both backends and ASan+UBSan. |
| CBIND-035B4d | Complete BoundingFrustum and close parent B4 | ✅ | `geometry.h` maps all 31 remaining BoundingFrustum rows through one corner-count constant and 22 operations. The matrix remains the direct POD property; construction derives all six plane queries and eight canonical corners on demand. Value-equal frusta preserve same-value containment, caller-capacity copies are atomic and native boundary-origin ray `NotImplementedException` becomes `CNA_RESULT_NOT_SUPPORTED` without output mutation. `GeometrySmoke.c` calls every entry point under both backends and ASan+UBSan. This closes parent CBIND-035B4. |

##### CBIND-035B5 curve slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035B5a | Complete CurveKey value operations | ✅ | `curve.h` maps all 19 CurveKey rows through a fixed 20-byte `CNA_CurveKey` and 17 operations covering every constructor/property, clone, comparison, equality/operator and hash route. Unknown continuity values are rejected before output mutation; native IEEE/NaN comparison behavior is preserved. Strict C17 and C++23 assertions freeze size, alignment and every field offset, while `CurveSmoke.c` calls every entry point under both backends and ASan+UBSan. |
| CBIND-035B5b | Complete CurveKeyCollection ownership and mutation | ✅ | `curve.h` maps all 26 CurveKeyCollection rows through an owned, generation/type/thread-validated handle and 14 operations. Count/get and atomic destination-index copy replace native aliases, indexers and all iterator routes; add/set preserve native position ordering, while clear/clone/contains/index/remove map collection behavior without leaking `std::vector`. `CurveSmoke.c` calls every entry point and covers ordering, repositioning, clone independence, capacity atomicity, invalid keys/indices and invalid/stale/wrong-thread handles under both backends and ASan+UBSan. |
| CBIND-035B5c | Complete Curve evaluation and close parent B5 | ✅ | `curve.h` maps all 15 Curve rows through a generation/type/thread-validated owned handle and 14 operations. Both native key-reference properties collapse to an owned mutable collection-view handle that retains the curve; creation/destruction, constant state, both loop properties, deep clone, evaluation and every tangent overload delegate to the canonical implementation. `CurveSmoke.c` calls every entry point and covers all five loop modes, all tangent overloads, clone independence, retained-view lifetime, invalid enums/indices and invalid/stale/wrong-thread handles under both backends and ASan+UBSan. This closes parent CBIND-035B5. |

##### CBIND-035B6 color slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035B6a | Complete Color value operations | ✅ | `color.h` maps all 25 previously planned non-constant Color rows through the existing four-byte `CNA_Color`, direct channel fields and 24 operations covering all constructors, packed value, exact/debug strings, conversions, equality/hash, Lerp, both premultiplication routes, multiplication/operators and packed-vector mutation. Integer premultiplication explicitly preserves FNA's unchecked Int32 product without C++ signed-overflow UB. `ColorSmoke.c` calls every entry point and covers ABI/order, clamp/truncation/wrap, exact strings, capacity atomicity and null failures under both backends and ASan+UBSan. |
| CBIND-035B6b | Complete named Color constants and close parent B6 | ✅ | `named_colors.h` maps all 141 public named colors to directly usable C17/C++23 `CNA_COLOR_*` value expressions built from exact RGBA channels. `ColorSmoke.c` passes every expression through the packed-value API and independently compares it with the canonical AABBGGRR literal, while public-header builds prove both language modes. This closes parent CBIND-035B6. |

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

## Completion criteria for the C ABI foundation

The initial milestone is complete only when all selected B0–B4 rows are ✅ and the following
statement is demonstrably true:

> A C program, compiled as C and including only `<CNA/C/cna.h>`, can create and run a small CNA
> game, receive lifecycle callbacks, clear a frame, upload a texture, submit a batched sprite draw,
> read a documented input snapshot, retrieve a UTF-8 error on failure, and release every owned
> resource without leaking or using a C++/Sharp Runtime ABI type.

This is an **experimental C ABI foundation**, not ABI 1.0 or a future language-specific binding.

## Completion criteria for full public CNA API coverage

The C API is not complete until `CBIND-044` is ✅ and the machine-checked coverage matrix proves
that every public CNA API symbol has a documented C-native mapping and the required C-only tests.
The full surface must preserve the behavior of the canonical C++ implementation (and FNA/XNA where
applicable), including constants, overload-specific behavior, errors, lifetime and renderer
capability limits. A raw C++ type, exception, container, callback/delegate, stream, task or Sharp
Runtime value is never an acceptable substitute for a C mapping.

## Current status

`CBIND-000` through `CBIND-034` and slice `CBIND-035A` are ✅; parent `CBIND-035` is 🟨 and
`CBIND-035B` through `CBIND-044` remain ⬜. The
exported ABI is still experimental `0.1.0`: it contains the version/error substrate, the HEADLESS-
and SDL_RENDERER-tested C game lifecycle slice, callback-scoped graphics capability discovery and
owned Color `Texture2D` bulk transfer, batched textured-quad submission, expanded input POD
snapshots and
SDL pixel-verified backbuffer readback, not complete public CNA coverage. No language-specific
binding exists. B4 is complete; B5 now includes owned content-manager/root/cache control and Color
Texture2D loads, keyboard/mouse/gamepad/touch capture and a PCM16 SoundEffect/instance control
route with stable native playback-availability reporting and isolated success/unavailable-device
regressions. B5 is complete. B6 has a deterministic, reviewed 414-header/6,415-symbol baseline and
now maps the full CBIND-034 graphics family through C-native state/display PODs, adapter queries,
owned render targets and SpriteFonts. CBIND-035A establishes the public 3D value and identity ABI
without claiming its still-unimplemented operations. CBIND-035B1 completes the Point and Rectangle
operation families, and completed CBIND-035B2a–B2d cover stateless MathHelper plus all Vector2/3/4
rows. Completed CBIND-035B3a–B3b cover Quaternion and Matrix. The current snapshot is 1,442
implemented, 21 partial, 4,882 planned and 70 not applicable. CBIND-035B4a completes Plane and Ray,
CBIND-035B4b completes BoundingBox, CBIND-035B4c completes BoundingSphere and CBIND-035B4d
completes BoundingFrustum, closing parent CBIND-035B4. The current snapshot is 1,577 implemented,
21 partial, 4,747 planned and 70 not applicable. CBIND-035B5a completes all 19 CurveKey rows; the
current snapshot is 1,596 implemented, 21 partial, 4,728 planned and 70 not applicable.
CBIND-035B5b completes all 26 CurveKeyCollection rows; the current snapshot is 1,622 implemented,
21 partial, 4,702 planned and 70 not applicable. CBIND-035B5c completes all 15 Curve rows and closes
parent B5; the current snapshot is 1,637 implemented, 21 partial, 4,687 planned and 70 not
applicable. CBIND-035B6a completes the remaining 25 non-constant Color rows; the current snapshot
is 1,662 implemented, 21 partial, 4,662 planned and 70 not applicable, with CBIND-035B6b named
Color constants next. CBIND-035B6b completes all 141 named Color rows and closes parent B6; the
current snapshot is 1,803 implemented, 21 partial, 4,521 planned and 70 not applicable, with
CBIND-035B7 PackedVector operations next.
