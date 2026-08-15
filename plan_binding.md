# CNA Native C Binding / Stable C ABI — Implementation Plan

> **Status: IMPLEMENTATION AUTHORIZED — B0–B5 complete; B6 complete through all of CBIND-035 and
> CBIND-036, plus CBIND-037A and CBIND-037B1–B3/B4a–B4c, verified under HEADLESS, SDL_RENDERER,
> SOFTWARE and a combined ASan+UBSan tree (2026-08-15). Coverage: 4,182 implemented / 2,082 planned
> — see *Current status* for the remaining order of work.** This document is
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
│   ├── content_readers.h     # compiled-asset readers and the type-reader registry
│   ├── net.h                 # network identities, values and packet buffers
│   ├── net_gamers.h          # network gamers, machines and event descriptions
│   ├── net_sessions.h        # discovered sessions, collections and network sessions
│   ├── gamer_services.h      # signed-in gamers (minimum the session slice needs)
│   ├── storage.h             # storage devices, containers and file streams
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
| CBIND-035 | Add 3D resources, effects, models and draw-submission coverage | ✅ | Design C-native vertex/index data layouts, effects/model/state handles and bulk submissions for all public APIs in these families. Require real-renderer correctness tests; do not claim all renderer parity from structural tests. Work is decomposed into CBIND-035A–G below; the parent becomes complete only when all seven rows and every CBIND-035 inventory row are closed. |
| CBIND-036 | Add stream, storage, networking and asynchronous-operation coverage | ✅ | Define stream callbacks, storage/network objects and neutral operation handles where the canonical API needs them, with documented ownership, thread, cancellation and error conversion. Never expose `System::IO::Stream`, `Task`, `std::future` or a C++ pointer. Work is decomposed into CBIND-036A–E below; the parent becomes complete only when all five rows and every CBIND-036 inventory row are closed. **Closed by CBIND-036E5:** no planned `storage`, `content` or `net` inventory row remains, and the snapshot is 3,841 implemented, 30 partial, 2,428 planned and 116 not applicable. Sanitizer evidence matches the CBIND-035B–E bar: all 50 C API tests pass under a combined ASan+UBSan `SOFTWARE`/`CNAEXT` build (`cmake-build-binding-asan`) **with leak detection enabled**, so the storage, content and network slices report no leak, no invalid access and no undefined behavior. |
| CBIND-037 | Add collections, events, services, media and devices coverage | 🟨 | Map every public collection/event/service/media/device API to count/copy, stable-handle or callback forms. Prohibit public container layouts and test mutation, capacity, ownership and thread rules. Work is decomposed into CBIND-037A–G below; the parent becomes complete only when all seven rows and every CBIND-037 inventory row are closed. |
| CBIND-043 | Maintain a machine-checked coverage gate | ✅ | A CI checker compares the public-header inventory to `COVERAGE.md` and fails if a public type/member/constant/event has no mapping/status. New C++ public API cannot land without its C API row and tests in the same change. **Done: the matrix is a gate now, not a report.** The generator and its `--check` mode already existed; `docs/c-api/COVERAGE.md` itself said making it mandatory was reserved for this task. Two places enforce it: the CTest test `CApiCoverageMatrix` (8 s, registered under `CNA_BUILD_TESTS` — deliberately NOT under `CNA_BUILD_C_API`, since the check compiles nothing and the rule is about the C++ surface, so gating it on the C API being built would mean the ordinary build never notices an unmapped symbol), and `.github/workflows/c-api-coverage-gate.yml`, which is build-free and therefore cheap enough to run on every push. **Proven to catch the thing it exists for:** adding one public declaration to `CNAHelper.hpp` turns the check from pass to "Coverage inventory is stale", naming the command that fixes it. The CI job also asserts the generator is DETERMINISTIC — `--write` must be a no-op after `--check` passes — because a generator that churns makes the matrix unreviewable; verified locally. |
| CBIND-044 | Close the public API coverage matrix | ⬜ | Every row is implemented and tested, or carries an owner-approved native limitation with a callable C API that reports it. No unspecified omission remains. |

### CBIND-035 implementation slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035A | Establish 3D value and identity ABI | ✅ | `math_values.h` now defines fixed-layout Point, Vector4, Quaternion, Matrix, Plane, Ray and bounding-volume PODs, all 17 public PackedVector raw-storage aliases and stable containment/plane/curve identities. `graphics3d.h` freezes buffer/index/primitive/SetData/vertex identities and the four-field `CNA_VertexElement`. Strict C17 and C++23 assertions cover every represented storage width, representative/full field offsets and identity ordinals under HEADLESS and SDL_RENDERER. Coverage maps only the 169 directly represented type/field/property/identity rows; all constructors, constants and operations remain owned by CBIND-035B. |
| CBIND-035B | Complete math, geometry and packed-value operations | ✅ | Every public math and PackedVector row is mapped through fixed values, validated handles or C-native scalar/bulk operations. Numeric, IEEE, lifetime, capacity, aliasing and failure behavior is covered in strict C under HEADLESS and SDL_RENDERER plus focused ASan+UBSan runs. Completed as CBIND-035B1–B7. |
| CBIND-035C | Add texture, buffer and vertex-resource coverage | ✅ | `texture.h`, `texture_volume.h`, `vertex_values.h`, `vertex_resources.h`, `index_resources.h` and the common `graphics_resource.h` map all 402 owned rows through fixed values, generation/type/thread-validated handles, caller-window transfers and explicit backend limits. Decomposed into and completed as CBIND-035C1–C7. |
| CBIND-035D | Add effects, shaders and parameter coverage | ✅ | All 653 Effect/technique/pass/parameter/annotation, stock/custom effect and shader/material rows are mapped without exposing bytecode objects, C++ containers or backend pointers. Completed by CBIND-035D1–D9 with strict-C HEADLESS/SDL_RENDERER and focused sanitizer evidence. |
| CBIND-035E | Add model, mesh and animation coverage | ✅ | Model/bone/mesh/part collections, morph and both skeletal-animation paths are mapped through stable handles, deep-copied descriptors, deterministic count/copy operations and tested resource lifetimes. |
| CBIND-035F | Complete graphics-device and draw submission | ✅ | Map remaining device properties/events/clear/present/draw overloads, viewport/scissor, texture collections and SpriteBatch transform/effect/text routes using validated descriptors and bulk submissions. Work is decomposed into CBIND-035F1–F7 below; the parent becomes complete only when all seven rows are closed. |
| CBIND-035G | Close and verify CBIND-035 | ✅ | No planned CBIND-035 inventory row remains: the snapshot is 3,476 implemented, 23 partial, 2,843 planned and 73 not applicable, and every remaining planned row belongs to CBIND-036, CBIND-037 or CBIND-044. `Draw3DSmoke.c` adds the missing real-output evidence: on a backend without the 3D capability it asserts deterministic refusal of buffer creation and all five draw routes, and on the CPU-raster SOFTWARE backend it clears to a known color and proves the center pixel changed through four independent routes — converted user primitives, indexed user primitives, buffered indexed geometry, and a full owned Model whose mesh part references real vertex/index buffers and a BasicEffect. Pixel readback is treated as a capability separate from 3D, so HEADLESS draws without claiming pixel evidence. Adding the third tree exposed three suites that branched on renderer *identity* rather than capability, contradicting this project's own rule that an enumerated identity is not a support claim; `CApi_TextureSmoke`, `CApi_TextureVolumeSmoke` and `CApi_LifecycleSmoke` now probe the actual behavior instead, which also turned SOFTWARE's real cube storage, mip upload and exact drawn texels into new positive evidence. All three trees are green at 47/47. This closes parent CBIND-035. |

#### CBIND-035B math implementation slices

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| CBIND-035B1 | Complete Point and Rectangle operations | ✅ | `math.h` maps both complete source headers through 37 exported operations covering constructors, named zero/empty values, every property/overload/operator, hashes and exact UTF-8 count/copy strings. Unsigned-bit arithmetic preserves C# unchecked 32-bit wraparound without C++ signed-overflow UB; division rejects zero and the unrepresentable minimum/-1 quotient without partial output. `MathValuesSmoke.c` calls every entry point and covers boundaries, mutation, half-open containment, intersection/union, capacity and null failures under HEADLESS, SDL_RENDERER and ASan+UBSan. |
| CBIND-035B2 | Complete MathHelper and Vector2/3/4 operations | ✅ | `math.h` and `vectors.h` map every MathHelper and Vector2/3/4 public inventory row through exact constants and fallible scalar/value/bulk operations. All overload-equivalent, finite/non-finite, exact-string, null/range-atomicity and sequential-aliasing contracts are covered in strict C under HEADLESS, SDL_RENDERER and ASan+UBSan. Completed as CBIND-035B2a–B2d. |
| CBIND-035B3 | Complete Quaternion and Matrix operations | ✅ | `quaternion.h` and `matrix.h` map every remaining public row through 85 fallible operations. Both constructors/constants/properties and all member/static/operator math, decomposition/interpolation/transformation/factory routes are covered with row-major, singular, projection-failure, non-finite and aliasing evidence under both backends and ASan+UBSan. Completed as CBIND-035B3a–B3b. |
| CBIND-035B4 | Complete planes, rays and bounding-volume operations | ✅ | `geometry.h` maps every remaining Plane, Ray, BoundingBox, BoundingSphere and BoundingFrustum row through C-native values, explicit optional hits and caller-capacity corners. Strict-C HEADLESS/SDL_RENDERER and ASan+UBSan tests cover every exported operation, including atomic capacity/failure paths and the canonical unsupported boundary-ray case. Completed as CBIND-035B4a–B4d. |
| CBIND-035B5 | Complete Curve value, collection and evaluation operations | ✅ | `curve.h` maps all 60 Curve, CurveKey and CurveKeyCollection rows through fixed values and validated handles without leaking C++ containers. Ordered collection mutation, retained mutable key views, all loop/evaluation/tangent behavior and lifetime/error boundaries are covered in strict C under both backends and ASan+UBSan. Completed as CBIND-035B5a–B5c. |
| CBIND-035B6 | Complete Color operations and named constants | ✅ | `color.h` and `named_colors.h` map the complete 175-row Color header through the four-byte POD, direct channels, 24 operations and 141 directly usable named value expressions. Every packed value is checked independently and all value/error behavior passes strict C under both backends and ASan+UBSan. Completed as CBIND-035B6a–B6b. |
| CBIND-035B7 | Complete PackedVector operations and close math coverage | ✅ | `packed_vectors.h` maps all 132 remaining concrete PackedVector, HalfTypeHelper and IPackedVector rows through 17 stable format identities, four generic format-tagged pack/unpack/equality operations and three half conversions. Raw/default constructors remain direct fixed-width values; specialized scalar/Vector2 routes collapse to the matching generic output. Integer formats reject non-finite consumed components before native conversion, half formats preserve IEEE special values, and narrower raw values reject upper bits without output mutation. `PackedVectorSmoke.c` covers every operation and format under both backends and ASan+UBSan; C/C++ assertions freeze every identity. This closes parent CBIND-035B. |

#### CBIND-035C resource implementation slices

The 402 rows owned by CBIND-035C are partitioned once by dependency boundary so each slice is
reviewable and independently committable. The counts below are inventory rows, not exported
function counts.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-035C1 | 104 | Complete built-in vertex values | ✅ | `vertex_values.h` maps the seven built-in `VertexPosition*` structures, all remaining `VertexElement` operations and `IVertexType` declaration routes through seven fixed POD layouts, a stable type tag and generic default/equality/hash/string/stride/element-copy operations. Parameterized constructors remain aggregate initialization. Strict C17 tests cover every operation/type, exact native strings and canonical GPU declarations; C/C++ assertions freeze identities, sizes and offsets under HEADLESS and SDL_RENDERER plus ASan+UBSan. |
| CBIND-035C2 | 14 | Complete vertex declarations and bindings | ✅ | `vertex_resources.h` owns standalone declarations through generation/type/thread-validated handles and maps empty, computed-stride and explicit-stride construction, exact type names, stride and atomic element copies without exposing native vectors. `CNA_VertexBufferBinding` is a fixed 16-byte descriptor: a zero aggregate is the default and its initializer validates a nonzero future buffer token plus nonnegative offset/frequency; actual token kind/generation remains a required consumption-time check in C6/F. Strict-C tests cover every route, invalid elements/ranges, capacity, wrong-kind/stale/wrong-thread handles and lifetime under both backends and ASan+UBSan; C/C++ assertions freeze both handle and descriptor layouts. |
| CBIND-035C3 | 21 | Complete the GraphicsResource common contract | ✅ | `graphics_resource.h` maps the complete base contract for Texture2D, RenderTarget2D, RenderTargetCube and VertexDeclaration handles: callback-scoped owning-device identity, disposal state and idempotent disposal, exact validated UTF-8 Name/ToString count-copy, a fixed C-owned 64-bit opaque tag and synchronous Disposing subscriptions with owned registration handles. Native `System::Object* Tag` and protected base construction/copy/move remain encapsulated. Strict-C tests cover every route across standalone and device-owned resources, generic/typed disposal, post-destroy unsubscription, capacity/encoding failures and wrong-kind/stale/wrong-thread handles under both backends plus ASan+UBSan; registry tests prove tag reset and C/C++ assertions freeze both public scalar handles. |
| CBIND-035C4 | 134 | Complete Texture and Texture2D | ✅ | `texture.h` completes all 134 previously unfinished rows plus the two inherited partial Texture properties through standalone/game-owned default, device, file, RGBA8, CPU-only and encoded-memory factories; common/2D/storage snapshots; all 18 native typed full/mip/rectangle transfer representations; the direct raw-RGBA8 `SetDataRGBA` route; format/block/alignment validation; exact type text; and PNG/JPEG count-copy/file routes. Streams and native renderer/weak pointers stay behind the ABI. Strict-C tests cover every route, all 27 formats, dispatch/rejection for every transfer identity, image/file round-trips, lifecycle and atomic failure cases under HEADLESS and SDL_RENDERER; HEADLESS proves mip upload, SDL maps its native mip-upload limit to `NOT_SUPPORTED`, and ASan+UBSan is clean. |
| CBIND-035C5 | 40 | Complete Texture3D and TextureCube | ✅ | `texture_volume.h` maps all 40 rows through owned game-child handles, versioned 3D/cube snapshots and full mip/box or six-face/rectangle Color transfer descriptors, including raw Texture3D bytes, exact type text, copied-memory DDS decoding, common Texture/GraphicsResource operations and RenderTargetCube inheritance. Native streams and renderer pointers stay private. The strict-C suite covers all faces, regions, capacity atomicity, lifecycle and invalid/stale/wrong-kind/wrong-thread paths under HEADLESS and SDL_RENDERER; both backends truthfully reject Texture3D creation and cube storage, while ASan+UBSan is clean. |
| CBIND-035C6 | 57 | Complete vertex buffers | ✅ | `vertex_resources.h` maps all 57 static/dynamic VertexBuffer rows through owned game-child handles, copied declaration metadata, versioned info and caller-array transfers for all seven built-in vertex types, raw bytes, all four dynamic option overloads, exact type text, generic GraphicsResource state and a distinct owned ContentLost registration. Count and window overloads preserve caller-array semantics and atomic readback without exposing CPU shadows or renderer pointers. Strict-C HEADLESS tests cover every value/option route, WriteOnly, disposal/events, capacity and invalid/stale/wrong-kind/wrong-thread paths; SDL_RENDERER verifies its no-3D capability as atomic `NOT_SUPPORTED`, and ASan+UBSan is clean. |
| CBIND-035C7 | 32 | Complete index buffers | ✅ | `index_resources.h` maps all 32 static/dynamic IndexBuffer rows through owned game-child handles, versioned metadata and caller-array transfer descriptors for both 16- and 32-bit indices, all dynamic streaming options, exact type text, generic GraphicsResource state and a distinct owned ContentLost registration. Count and window overloads preserve caller-array semantics with copied/aligned input and atomic scratch readback. Strict-C HEADLESS tests cover both widths/kinds/options, WriteOnly, disposal/events, capacity and invalid/stale/wrong-kind/wrong-thread paths; SDL_RENDERER verifies its no-3D capability as atomic `NOT_SUPPORTED`, C/C++ ABI assertions freeze all descriptors and ASan+UBSan is clean. This closes parent CBIND-035C. |

#### CBIND-035D effect implementation slices

The 653 rows owned by CBIND-035D are partitioned by dependency boundary. Collection and stock
effect slices build only on the earlier identity/value/handle contracts; no slice exposes native
containers, shader objects or renderer pointers.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-035D1 | 17 | Establish effect-parameter identities | ✅ | `effects.h` defines fixed-width `CNA_EffectParameterClass` and `CNA_EffectParameterType` identities with all five class and ten type constants at their native ordinals. Strict C17 and C++23 assertions cover every value and storage width under HEADLESS and SDL_RENDERER. |
| CBIND-035D2 | 30 | Complete effect annotations | ✅ | `effects.h` maps all EffectAnnotation/Collection rows through owned immutable annotation handles created from copied UTF-8/raw-value metadata and owned mutable collection snapshots. Versioned info, exact strings, every scalar/vector/matrix getter and copied count/index/name operations preserve empty/default/native bit-storage behavior without exposing references, vectors or iterators. Strict-C tests cover all operations, collection-copy independence, capacity atomicity and invalid/stale/wrong-kind/wrong-thread paths under both backends plus ASan+UBSan; C/C++ assertions freeze handle and descriptor layouts. |
| CBIND-035D3 | 84 | Complete effect parameters | ✅ | `effects.h` maps all EffectParameter/Collection rows through owned mutable handles and stable collection-element aliases. Versioned copied metadata, exact strings, tagged scalar/array values, distinct matrix-transpose and texture overload dispatch, nested element/member/annotation views and count/index/name/semantic operations expose no C++ references, vectors or iterators. Assigned texture handles are retained per overload slot. Strict-C tests cover every value family, defaults, stable growth/destruction aliases, nesting, Texture2D retention, capacity atomicity and invalid/stale/wrong-kind/wrong-thread paths under both backends plus ASan+UBSan; C/C++ assertions freeze descriptor and tag layouts. |
| CBIND-035D4 | 67 | Complete techniques, passes and collections | ✅ | `effects.h` maps EffectTechnique/EffectPass and both collection families through owned handles and stable collection-element aliases. Both technique constructors, canonical `P0`, exact names, non-pointer identities, nested pass/annotation views, canonical Apply dispatch and construction-plus-add/count/index/name operations replace owner pointers, references, vectors and iterators. Strict-C tests cover construction, identity, ownerless native Apply, nesting, stable aliases across growth/destruction, capacity atomicity and invalid/stale/wrong-kind/wrong-thread paths under both backends plus ASan+UBSan; effect-owned current-technique validation uses the same route once D5 supplies effect lifecycle handles. |
| CBIND-035D5 | 70 | Complete Effect, ShaderEffect, EffectMaterial and SpriteEffect | ✅ | `effects.h` maps all 68 callable rows plus the two explicitly deleted/non-callable copy operations through owned game-child `CNA_EffectHandle` values: a minimal concrete base adapter, native EffectMaterial/ShaderEffect/SpriteEffect construction, same-type clone, dispose/apply, borrowed device identity, stable parameter/technique/current-technique views, exact type/source strings, shader validity/renderer queries, all uniform/texture/matrix routes and exact stock-sprite recognition. Compiled XNA `.fx` bytecode returns the native callable `NOT_SUPPORTED` limitation; renderer pointers/GpuDrawParams remain private behind Apply/draw paths. Strict-C tests cover lifecycle, current-pass validation, transitive descendant lifetime after parent destruction, texture retention, all shader calls and invalid/stale/wrong-kind/wrong-thread paths under both backends plus ASan+UBSan; C/C++ assertions freeze the handle. |
| CBIND-035D6 | 90 | Complete BasicEffect, DirectionalLight and effect interfaces | ✅ | `effects.h` maps all 90 BasicEffect, DirectionalLight and IEffectMatrices/Fog/Lights rows through owned BasicEffect handles, standalone or stable nested directional-light handles and generic interface operations reusable by later stock effects. All transform, fog, lighting, vertex-color, material, alpha, texture and per-pixel properties plus exact three-light/default-lighting behavior are exposed. Same-device Texture2D assignments are retained and cloned safely; live nested light aliases transitively retain their effect and game after the parent handle is destroyed. Renderer-only GpuDrawParams stays behind Apply/draw. Strict-C tests cover exact defaults/preset constants, every operation, clone/retention/lifetime and invalid/stale/wrong-kind/wrong-thread paths under both backends plus ASan+UBSan; C/C++ assertions freeze the light handle. |
| CBIND-035D7 | 114 | Complete AlphaTest, DualTexture and EnvironmentMap effects | ✅ | `effects.h` maps all 114 AlphaTestEffect, DualTextureEffect and EnvironmentMapEffect rows through owned game-child effect handles, shared lifecycle/type/matrix/fog/light routes and complete concrete material, alpha-test, two-layer and environment-map state. Texture2D and TextureCube assignments require the same graphics device, retain their C resources per slot and are copied independently into native clones; invalid enums/bools/indices are rejected while native unclamped signed ReferenceAlpha and stock scalar behavior are preserved. EnvironmentMapEffect's always-on lighting maps a false setter to `INVALID_STATE`, and renderer-only GpuDrawParams stays behind Apply/draw. Strict-C tests cover exact defaults, every operation, clone/retention/lifetime, cross-owner refusal and invalid/stale/wrong-kind/wrong-thread paths under both backends plus ASan+UBSan. |
| CBIND-035D8 | 52 | Complete SkinnedEffect | ✅ | `effects.h` maps all 52 SkinnedEffect rows through an owned game-child effect, the shared lifecycle/type/matrix/fog/light routes and complete material, per-pixel, texture, weights and CNA vertex-color state. `CNA_SKINNED_EFFECT_MAX_BONES` freezes the native 72-matrix maximum; copied set and atomic count/capacity copy operations preserve the native one-through-72 bounds and identity defaults without exposing vectors. Texture2D assignments require the same graphics device and retain independently across clones; always-on lighting maps false to `INVALID_STATE`, while GpuDrawParams remains behind Apply/draw. Strict-C tests cover all defaults, every operation, exact bone transfer, bounds/capacity errors, texture clone retention, nested-light lifetime and invalid/stale/wrong-kind/wrong-thread paths under both backends plus ASan+UBSan; C/C++ assertions freeze MaxBones. |
| CBIND-035D9 | 129 | Complete ColorMatrix, PbrEffect and SkinnedPbrEffect extensions | ✅ | `effects.h` maps all 129 extension rows through owned game-child effects, a fixed 64-byte row-major color matrix, shared PBR material/fog/light/matrix routes, five retained Texture2D slots and a fixed 72-bone SkinnedPbr palette. Matrix/offset inputs reject non-finite values; always-on PBR lighting maps false to `INVALID_STATE`; copied palette operations preserve one-through-72 bounds and atomic capacity behavior. Strict-C tests cover exact defaults, all operations, clone/resource retention, nested-light lifetime and invalid/stale/wrong-kind/wrong-thread paths under HEADLESS and SDL_RENDERER plus ASan+UBSan; C/C++ assertions freeze the POD, slot identities and MaxBones. This closes parent CBIND-035D. |

#### CBIND-035E model and animation implementation slices

The 178 rows owned by CBIND-035E are partitioned by stable-handle dependency. Bone, part and mesh
views land before aggregate Model ownership; standalone morph/skinning data then supports the
animation-player layer. Native containers and iterators collapse to live count/index/name views or
copied bulk transfers.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-035E1 | 23 | Complete ModelBone and ModelBoneCollection | ✅ | `models.h` maps standalone and hierarchy-owned bones through stable handles, exact UTF-8 names, signed indices, copied transforms, optional parent views, retained child relationships and live count/index/name/contains collections. Self-parenting and ancestor cycles are rejected; weak parent metadata prevents a retained child from exposing a dangling native pointer. Strict-C tests cover every route, hierarchy lifetime and invalid/stale/wrong-kind/wrong-thread behavior under HEADLESS and SDL_RENDERER plus ASan+UBSan; C/C++ assertions freeze both handles. |
| CBIND-035E2 | 28 | Complete ModelMeshPart and its collection | ✅ | `models.h` maps both constructors, exact signed scalar state, optional same-device Effect/VertexBuffer/IndexBuffer associations and a C-owned opaque 64-bit tag through stable shared handles. Snapshot collections retain parts and replace native pointers/iterators with count/index aliases; the representation is ready for ModelMesh-owned live views in E3. Retained graphics resources reject typed destroy and generic dispose until cleared or the final part alias is released. Strict-C tests cover state, alias/snapshot lifetime, handle/thread/array errors and supported-buffer or honest renderer-refusal paths under HEADLESS and SDL_RENDERER plus ASan+UBSan; C/C++ assertions freeze both handles and the tag. |
| CBIND-035E3 | 38 | Complete ModelMesh, ModelMeshCollection and ModelEffectCollection | ✅ | `models.h` maps both game-child ModelMesh constructors, exact UTF-8 names, bounding sphere/tag/retained-parent state, live part/effect views and capability-gated Draw. Retained mesh snapshots expose count/index/find/contains aliases; live effect views preserve duplicate Add, first-match Remove and identity while blocking early effect disposal. Parts belong to one live mesh and switch to synchronized detached native state when its final owner expires, preventing dangling parent access. Strict-C tests cover every route, transitive aliases, resource lifetime, invalid inputs/threading and HEADLESS success versus SDL_RENDERER Draw refusal plus ASan+UBSan; C/C++ assertions freeze all handles/tags. |
| CBIND-035E4 | 14 | Complete Model | ✅ | `models.h` maps the default, aggregate and explicit parent/root constructors through `CNA_ModelHandle`; bone/mesh/root aliases retain stable objects, tags are opaque 64-bit values, and native `shared_ptr<void>` ownership becomes a C context/release callback with deterministic replacement/clear/destruction. Count/copy local and absolute transforms are capacity-atomic, transform input is copied before mutation, and non-empty Draw is capability-gated. Strict-C tests cover every route, parent composition, transitive lifetime, invalid arrays/root/counts, callback releases and thread/renderer errors under HEADLESS and SDL_RENDERER plus ASan+UBSan; C/C++ assertions freeze handle/tag widths. |
| CBIND-035E5 | 20 | Complete morph-target extension values and operations | ✅ | `models.h` maps keyframes, tracks and target deltas through fixed deep-copied descriptors and owns validated MorphTargetDataEXT handles. Atomic count/copy routes expose every nested field without C++ vectors; mutable weights/tracks, LINEAR/STEP/Hermite evaluation, base-byte blending, retained ModelMeshPart attachment and supported VertexBuffer upload map all native operations. Strict-C tests cover ABI, exact blend/evaluation math, malformed shapes/flags/times/counts, capacity atomicity, lifetime and handle/thread errors under HEADLESS and SDL_RENDERER plus ASan+UBSan. |
| CBIND-035E6 | 36 | Complete SkinnedModelEXT | ✅ | `CNA_SkinnedModelEXTHandle` deeply copies fixed skeleton/keyframe/track/clip descriptors, exposes deterministic count/copy and native transform-sampling routes, and supports normalized native move semantics. A stable lifetime sidecar retains same-device VertexBuffer/IndexBuffer/ModelMeshPart/optional Texture2D resources, blocks premature disposal and maps ordered part access, replace-by-name attach/remove and owned counts. Strict-C tests cover all layouts and operations, exact interpolation/clamp/loop math, validation/capacity atomicity, moves, lifetime and handle/thread failures under HEADLESS and SDL_RENDERER on dummy virtual video plus ASan+UBSan. |
| CBIND-035E7 | 19 | Complete SkinningData and AnimationPlayer | ✅ | `CNA_SkinningDataHandle` deeply copies validated hierarchy/bind/inverse-bind/root-prefix/named-clip state and exposes type, deterministic clip and atomic field copies. `CNA_AnimationPlayerHandle` retains data, starts exact named clips, maps relative/absolute loop/clamp Update and exposes position/current clip plus atomic local/world/skin matrices. Strict-C tests cover every route, deep-copy/lifetime, exact prefix/interpolation composition, capacity and input/handle/thread failures under HEADLESS and SDL_RENDERER plus ASan+UBSan; this closes parent CBIND-035E. |

#### CBIND-036 stream, storage and networking implementation slices

The 406 rows owned by CBIND-036 are partitioned once by dependency boundary. One boundary was
corrected while implementing: `LocalNetworkGamer` moved from CBIND-036D to CBIND-036E, because its
receive and send paths dereference the owning session, so the 65/104 split became 47/122. Storage lands first
because it is self-contained and introduces the C stream contract every later file-facing row
reuses; content follows; the networking families are ordered so identities, values and packet
buffers exist before the session and gamer objects that consume them.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-036A | 42 | Complete storage devices, containers and file streams | ✅ | `storage.h` and `CnaCApiStorage.cpp` map every `storage` row: owned `CNA_StorageDeviceHandle`, `CNA_StorageContainerHandle` and `CNA_StorageStreamHandle` families that nest strictly and refuse destruction while a child is live; free/total space, connection state, both events (the static `DeviceChanged` without a device handle, per-instance `Disposing` through one), container display/type-name count-copy, directory and file create/exists/delete, both listing overloads as count plus indexed copy with an empty pattern selecting the no-argument overload, `CreateFile` and all three `OpenFile` overloads, and container deletion keeping the canonical containment guard. All four `BeginShowSelector`/`EndShowSelector` pairs and `BeginOpenContainer`/`EndOpenContainer` collapse into single synchronous calls that still invoke the canonical completion callback, so no `System::IAsyncResult` or invented operation handle exists in C. `System::IO::Stream` stays behind the adapter; wider-than-`Int32` counts are refused rather than truncated and stream capabilities are queried, not inferred. `filesystem_error`, `System::IO::IOException` and `StorageDeviceNotConnectedException` gained boundary conversions to `CNA_RESULT_IO`, `CNA_RESULT_IO` and `CNA_RESULT_INVALID_STATE`, each proven in `cna_c_api_boundary_detail_test`. Strict-C `StorageSmoke.c` plus C/C++ ABI assertions run green in all three trees (48/48). The snapshot is now 3,518 implemented, 23 partial, 2,801 planned and 73 not applicable, with no planned `storage` row left. |
| CBIND-036B | 97 | Complete content readers, managers and manifests | ✅ | Map `ContentReader`, the remaining `ContentManager` rows, `ContentTypeReader`/`ContentTypeReaderManager`, `ContentManifestEntry`, `ResourceContentManager`, `LooseFileContentTypeReader`, `KnownUnsupportedContentTypeReader` and `ContentLoadException` without exposing C++ type-reader templates, streams or containers. Decomposed into CBIND-036B1–B2 below. |
| CBIND-036C | 98 | Complete network identities, values and packet transfer | ✅ | `net.h` and `CnaCApiNet.cpp` map all five identity enumerations at their canonical ordinals, the `CNA_QualityOfService` value with both canonical factories, an owned `CNA_NetworkSessionPropertiesHandle` over the optional-integer list with an owned enumerator handle, and owned packet read/write buffers with one route per canonical read and write. Two canonical behaviors are preserved rather than tidied up -- the list reports itself read-only while still mutating, and an out-of-range write appends -- and two are decided in C because the canonical implementation does not decide them at all: `Insert`/`RemoveAt` are range-checked before the unchecked native call, and the enumerator's before-first read becomes `CNA_RESULT_INVALID_STATE` instead of an out-of-bounds dereference. The canonical color write/read asymmetry is preserved and proved in both directions. `NetworkSessionJoinException` converts to `CNA_RESULT_INVALID_STATE` and its join error is recorded per thread for `cna_net_get_last_join_error`, cleared by any later failure; the conversion is proved in `cna_c_api_boundary_detail_test`. Two `_ext` routes move packet bytes because the canonical API never exposes them. Strict-C `NetSmoke.c` plus C/C++ ABI assertions run green in all three trees (50/50). |
| CBIND-036D | 47 | Complete network gamers, machines and events | ✅ | `net_gamers.h` and `CnaCApiNetGamers.cpp` map `NetworkGamer`, `NetworkMachine` and all seven event-argument types: an owned gamer handle carrying every canonical flag, the session-local identifier, the round-trip time as ticks and the owning session handle, with the CNA extension setters kept under an `_ext` suffix so a consumer can see which state the canonical API otherwise leaves fixed; an owned machine handle with a counted roster whose borrowed gamer views block the machine's release, and a removal route that reports the canonical always-throwing placeholder as `NOT_SUPPORTED`; and seven fixed `CNA_*EventInfo` descriptions with `_init` routines that validate their payload gamer handle. `getMachineProperty` hands back an independent copy because the canonical setter already takes its machine by value and the canonical machine exposes no mutator. **Re-partitioned:** `LocalNetworkGamer` moved to CBIND-036E — its receive and send paths dereference the owning session, so it cannot exist before sessions do. `NetSmoke.c` grew the gamer, machine and event coverage and runs green in all three trees (50/50). |
| CBIND-036E | 122 | Complete network sessions, local gamers and discovery | ✅ | Map `NetworkSession`, `LocalNetworkGamer`, `AvailableNetworkSession` and `AvailableNetworkSessionCollection`, including creation/find/join, session state, packet send and receive and every session event, through owned handles, count/copy collections and documented asynchronous-operation conversion. `LocalNetworkGamer` moved here from CBIND-036D because its receive and send paths dereference the owning session. |

#### CBIND-036E session implementation slices

The 122 rows CBIND-036E owns are partitioned by what each part needs to exist. Available sessions
are a self-contained discovery value and land first; the session object then arrives in three
passes — its own state and identity, its ten events, and the creation/discovery/join surfaces whose
fake-async pairs need the session object to already exist — and the local gamer lands last, because
its receive and send paths dereference the session it belongs to.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-036E1 | 17 | Complete available sessions and their collection | ✅ | `net_sessions.h` and `CnaCApiNetSessions.cpp` map both types: an owned discovered-session handle built from a versioned creation structure, every scalar property, count/copy host gamertag and connect address, connect port and session type, a copied `CNA_QualityOfService` and an independently copied property list, and both equality operators as explicit routes. Only the round-trip sample can be carried into a quality of service, because that is the only measurement the canonical type accepts. The collection is an owned handle with disposal state, explicit dispose and release, a count and an indexed copy-out whose element survives the collection it came from. `NetSmoke.c` grew the discovered-session coverage and runs green in all three trees (50/50). |
| CBIND-036E2 | 60 | Complete session identity, state and gamer management | ✅ | `net_sessions.h` gains an owned `CNA_NetworkSessionHandle` that owns the caller-owned pointer canonical creation returns, both limits as constants, the queued-event identities with a fixed `CNA_NetworkEventInfo`, every state property and setter, all four rosters as a roster identity plus count and indexed borrowed views, an independently copied property list, the exact type name, disposal, the pump, local-gamer addition, identifier lookup, ready reset, start and end, and the three CNA extension routes. A borrowed gamer view blocks its session's release, and a remote gamer is retained by the C layer because the canonical add deliberately does not take ownership. **Re-partitioned:** the three `Create` overloads moved here from CBIND-036E4 (57→60 and 20→17), because none of this slice's state is reachable without a session object. **Borrowed from CBIND-037:** the canonical session constructor selects its host from its local gamers and therefore cannot run with no signed-in gamer, so `gamer_services.h` maps the minimum needed — `SignedInGamer::CreateInternal`, `SignedInGamerCollection::CreateInternal`, both `Gamer` signed-in collection accessors and the gamertag — five rows recorded against this task. `NetSmoke.c` grew the session coverage and runs green in all three trees (50/50). |
| CBIND-036E3 | 10 | Complete session event registrations | ✅ | One `cna_network_session_subscribe_*` route per event, each with a typed callback that receives the matching `CNA_*EventInfo` description, plus one shared `cna_network_session_unsubscribe`. A payload gamer is handed over as a handle that lives only for the duration of the callback, so a consumer can never retain a pointer into session-owned state. An instance registration holds a weak reference to its session, so releasing it after the session is gone is a no-op; `InviteAccepted` is static and its subscription belongs to the process. **Borrowed from CBIND-037:** the four `InviteAcceptedEventArgs` rows, mapped to `CNA_InviteAcceptedEventInfo`, because this slice maps the event that carries them. `NetSmoke.c` proves the canonical gamer-joined replay, real join/leave/start/end/host-change/session-end deliveries through the pump, and stale registration refusal; all three trees stay green (50/50). |
| CBIND-036E4 | 17 | Complete session discovery, join and the fake-async pairs | ✅ | Every `Begin`/`End` pair collapses into one synchronous C route that still invokes the canonical completion delegate, because CNA completes the pair before `Begin` returns; the delegate receives only the caller's own context, and no `System::IAsyncResult` or `std::any` is exposed. The three asynchronous creations are deliberately not aliases of the synchronous ones — the canonical end step substitutes its own gamer limit instead of forwarding the caller's, and `NetSmoke.c` asserts that difference. Both `Find` overloads, both asynchronous searches, `Join`, `JoinInvited` and their asynchronous forms are mapped, and the canonical refusal of a local-only search type plus the invited path's fixed session type are preserved. All three trees stay green (50/50). |
| CBIND-036E5 | 18 | Complete local network gamers | ✅ | `net_sessions.h` and `CnaCApiNetSessions.cpp` map `LocalNetworkGamer` over the same owned `CNA_NetworkGamerHandle`, because a local gamer *is* a network gamer; every route refuses a handle whose gamer is not local with `CNA_RESULT_INVALID_HANDLE` rather than reinterpreting it. The data-available and backing signed-in-gamer queries, all three `ReceiveData` overloads, all six `SendData` overloads including both `PacketWriter` forms, the canonical internal factory as `cna_local_network_gamer_create_ext` and the two CNAEXT queue routes are mapped; a payload crosses as a pointer plus a byte count and the sender comes back as a borrowed gamer view that keeps its session alive. Three canonical behaviors are preserved and asserted rather than tidied up: the offset receive consumes its packet **before** rejecting an out-of-range offset, the packet-reader receive always reports zero bytes even when it consumed a packet, and `EnableSendVoice`/`SendPartyInvites` are declared no-ops whose routes validate and succeed without pretending to do more. `NetSmoke.c` grew the local-gamer coverage and runs green in all three trees (50/50). This closes parent CBIND-036: the `net` module has no planned row left. |

#### CBIND-037 remaining-module implementation slices

The 2,428 rows CBIND-037 owns are partitioned once, by module — which here is also the dependency
boundary, because each module is its own library, its own include tree and its own C header family.
The order is by what each part needs to exist: `core` has no dependency at all and goes first; the
leaf device and content families follow; `runtime` comes after them because `Game` composes the
graphics, input and audio surfaces; and `gamer-services`, the largest, comes last because its guide
and dispatcher surfaces sit on top of the runtime. A slice larger than roughly a hundred rows is
sub-partitioned when it is reached, as CBIND-035 and CBIND-036 were.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-037A | 72 | Complete the CNA core module | ✅ | `core_ext.h` and `CnaCApiCoreExt.cpp` map every `core` row: one `cna_logger_*` route per canonical static so C never depends on a defaulted argument, the process-wide minimum level, the compile-time platform and desktop operating system, both backend classifications for any of the 46 public renderer identities plus their compiled-in forms, and the compiled-in renderer identity and name. Names use the project's count/copy pair rather than the canonical static-storage `std::string_view`, so no pointer into CNA storage crosses the ABI. `CNA::CNAException` gained a central boundary conversion to `CNA_RESULT_INVALID_STATE`, which is what makes the canonical non-desktop refusal of `getCurrentDesktopOS` observable in C instead of collapsing into a generic internal failure. The canonical `EXPERIMENT` log level keeps its ordinal 100 rather than being renumbered into a dense range, and 6 is refused. `CNAEXT` is `not-applicable`: a documentation-only marker macro with no callable behavior. Strict-C `CoreExtSmoke.c` plus C/C++ ABI assertions and two new `cna_c_api_boundary_detail_test` return codes run green in all three trees (51/51) and under ASan+UBSan with leak detection on. The `core` module has no planned row left. |
| CBIND-037B | 599 | Complete the input module | ✅ | `GamePadCapabilities`, the remaining `GamePad`/`Mouse`/`Keyboard`/`TouchPanel` surfaces, `MouseCursor`, `TextInputEXT`, the touch collection and gesture types, and the whole `CNA::Input` extension family (haptics, joysticks, sensors, clipboard, power, device enumeration) are mapped. Decomposed into CBIND-037B1–B7 below; **closed by CBIND-037B7b**, after which the `input` module records 834 implemented, 0 partial, 0 planned and 27 not applicable. |
| CBIND-037C | 325 | Complete the media module | ✅ | Map `MediaPlayer`, `Song`, `VideoPlayer`, `Video`, the media library and every media collection through count/copy collections and owned handles, without exposing a native stream or decoder. Decomposed into CBIND-037C1–C7 below; **closed by CBIND-037C7**, after which the `media` module records 276 implemented, 0 partial, 0 planned and 52 not applicable. |
| CBIND-037D | 289 | Complete the devices and devices-ext modules | 🟨 | Map the `Microsoft::Devices::Sensors` family, `VibrateController`, and the `CNA::Devices` extensions (camera, clipboard, file dialog, message box, system tray, power, locale, display and system info). Decomposed into CBIND-037D1–D4 below; the parent becomes complete only when all four rows and every `devices`/`devices-ext` inventory row are closed. |
| CBIND-037E | 273 | Complete the runtime module | ⬜ | Map the remaining `Game`, `GameWindow` and `GraphicsDeviceManager` surfaces, the game-component collection and its events, the service container, and the drawable/updateable contracts. |
| CBIND-037F | 205 | Complete the audio module | ⬜ | Map the remaining `SoundEffect`/`SoundEffectInstance` rows, `DynamicSoundEffectInstance`, `Microphone`, the XACT family (`AudioEngine`, `SoundBank`, `WaveBank`, `Cue`, `AudioCategory`), 3D audio and `FrameworkDispatcher`. |
| CBIND-037G | 665 | Complete the gamer-services module | ⬜ | Map the remaining gamer, profile, presence, privilege, achievement, leaderboard, avatar and guide surfaces on top of the minimum signed-in-gamer surface CBIND-036E2 and E3 already borrowed. |

#### CBIND-037B input implementation slices

The 599 rows CBIND-037B owns split by device family, and within the gamepad family by what each
part needs to exist: the capabilities value is independent, the state values compose into a
snapshot, and the `GamePad` statics need both.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-037B1 | 86 | Complete gamepad capabilities and controller type | ✅ | `input_gamepad.h` maps `GamePadType` at its canonical ordinals and the whole `GamePadCapabilities` surface as one fixed 48-byte value: `struct_size`/`struct_version`, the controller type and 35 directly readable **and writable** flags, because every canonical property has both a getter and a setter. A value rather than a handle, since the canonical type is a copyable snapshot with no identity, and direct fields rather than 74 routes, since that is what the getter/setter pair means in C. The ten CNA extension properties keep an `_ext` suffix on their fields. `cna_gamepad_capabilities_init` reproduces the canonical default constructor exactly. **Borrowed from CBIND-037B3:** `GamePad::GetCapabilities` is mapped here as `cna_gamepad_get_capabilities` (86 rows here, 24 left there), because a capabilities value with no producer cannot be tested against anything real. It takes an active game handle for the same reason `cna_gamepad_get_state` does, and an empty slot is an ordinary answer rather than a failure. `InputSnapshotsSmoke.c` cross-checks connection against the state snapshot so the two can never disagree, and runs green in all three trees (51/51) plus ASan+UBSan with leak detection on. |
| CBIND-037B2 | 65 | Complete gamepad state values | ✅ | `input_gamepad.h` maps all five canonical value types onto the representations the C API already had, so the ABI never grows a second spelling of the same numbers. `GamePadButtons` and `GamePadDPad` are the existing `CNA_GamePadButtonFlags` mask — the pad restricted to its four bits — because that is exactly what each canonical type holds and how CNA itself derives one from the other. `CNA_GamePadThumbSticks` (16 B) and `CNA_GamePadTriggers` (8 B) are new plain values that are **byte-identical to the two halves of the analog block a snapshot already carried**, asserted rather than assumed. The eleven named button getters and the four pad getters each collapse into one `_is_pressed` route that answers through the canonical getter owning the button. Three canonical behaviors are preserved and asserted rather than tidied up: the thumbstick constructor square-clamps to ±1 and the trigger constructor clamps to 0..1; trigger equality is an **epsilon** comparison, proved with the next representable float above a value; and the pad hash uses its own weighting (Down 1, Left 2, Right 4, Up 8), not the button bits. `GamePadState` gains both public constructions with their derived trigger and virtual-stick bits, the four component projections, the `_ext` packet-number setter, equality, the hash that mixes only buttons and packet number, and the fixed type-name string. One representational limit is documented, not hidden: the C snapshot carries a single button mask — as does every state CNA itself builds, since the capture path derives both the button set and the pad from one raw mask — so a supplied pad is merged into the button set. The `Buttons` flag operators need no route: C composes the `uint32_t` identity with its own operators, and every route validates against `CNA_GAMEPAD_BUTTON_ALL`. `InputSnapshotsSmoke.c` grew four validators with per-family return codes and runs green in all three trees (51/51) plus ASan+UBSan with leak detection on. |
| CBIND-037B3 | 45 | Complete the GamePad statics | ✅ | `input_gamepad.h` maps every remaining `GamePad` static as one `cna_gamepad_*` route, each taking an active game handle for the same reason the state and capability captures do: CNA is event-driven, so a device query is only meaningful on the game thread of a running game. `ExcludeAxisDeadZone` is the exception and takes none, being a pure value operation; the three dead-zone constants were already exposed as macros carrying the canonical expressions verbatim. Two canonical shapes are preserved rather than collapsed: a query reporting availability through its return value and its answer through an output reference keeps **both** answers separate in C, so "no sensor" is an ordinary answer rather than a failure, and the four identity strings use the project count/copy protocol. The touchpad finger query's four output references become one fixed 16-byte `CNA_GamePadTouchpadFinger`. No `std::string`, `Vector3` or CNA::Input enumeration crosses the boundary. **Borrowed from CBIND-037B7:** `GamePadButtonLabelEXT`, `GamePadConnectionStateEXT` and `PowerStateEXT` (21 rows), because three of these statics return them and cannot be mapped without them — 45 rows here, 117 left there. `InputSnapshotsSmoke.c` asserts the shape of an empty-slot answer for every route, the count/copy round trip for all four identity strings, and the per-route out-of-range, null-output, undefined-bit and wrong-thread refusals; all three trees green (51/51) plus ASan+UBSan with leak detection on. |
| CBIND-037B4 | 80 | Complete keyboard, mouse and text input | ✅ | Map the remaining `Keyboard`/`KeyboardState`, `Mouse`/`MouseState`, `KeyState`, `MouseCursor` and `TextInputEXT` rows. Split further by device, because the cursor is an owned disposable type and the text-input surface is event-driven while the keyboard and mouse are plain snapshots. Closed by `CBIND-037B4d`; all four sub-slices are complete and no planned row remains in any of their headers. |
| CBIND-037B4a | 35 | Complete the keyboard | ✅ | `input_keyboard.h` maps `KeyState`, the whole `KeyboardState` value surface and every `Keyboard` static over the versioned 256-slot bit field the C API already had. `cna_keyboard_state_init_from_keys` maps **both** canonical set-taking constructors, since an initializer list and an unordered set are the same deduplicated array in C. **Documented deviation:** the canonical constructors silently drop a key outside the 256-slot field; C refuses instead, so a caller can never lose a key without being told, and the refusal matches every other keyboard route. The player-slot `GetState` overload reports the same snapshot for every slot, because CNA has one keyboard. Both name families use the project count/copy protocol with borrowed `CNA_StringView` reverse lookups, so no `std::string` crosses the boundary, and an unknown name answers with the canonical none identity rather than failing. **Borrowed from CBIND-037B7:** `KeyModifiersEXT` and its five operators (15 rows), because `GetModStateEXT` returns it — 35 rows here, 102 left there. The flag operators need no route: unlike the gamepad button identities these really are flags, and C masks them with its own operators. `InputSnapshotsSmoke.c` covers every value operation and query plus their refusals, green in all three trees (51/51) and under ASan+UBSan with leak detection on. |
| CBIND-037B4b | 21 | Complete the mouse | ✅ | `input_mouse.h` maps the whole `MouseState` value surface and every remaining `Mouse` static. Each construction takes the same `CNA_MouseButtonFlags` bit set the snapshot already carries rather than five separately ordered button-state arguments, so a consumer cannot silently transpose two of them; the eight-argument form leaves the horizontal wheel at zero exactly as the canonical one does. Unlike the gamepad and keyboard snapshots this type **does** override its string conversion, and C reproduces the canonical format exactly, `None` included. The window handle crosses as an opaque `uint64_t` the C API never dereferences. A request no backend can satisfy answers `CNA_FALSE` through an applied output rather than failing, and the global-position query — canonically `void` with two output references — cannot fail at all. The static `ClickedEXT` event becomes an owned `CNA_MouseEventRegistrationHandle` that takes no game handle, because the canonical event belongs to the process; `INTERNAL_onClicked` becomes the raise route that makes it observable without a device, and `ResetForTests` is documented as dropping every subscription, including ones this API handed out, so a release afterwards is a no-op. **Re-partitioned:** `Mouse::SetCursor` moved to CBIND-037B4c (21 rows here, 22 there), because it cannot be mapped before a cursor handle exists. `InputSnapshotsSmoke.c` proves the click round trip and the reset-drops-subscriptions behavior; green in all three trees (51/51) and under ASan+UBSan with leak detection on. |
| CBIND-037B4c | 22 | Complete the mouse cursor | ✅ | `input_cursor.h` maps `MouseCursor` as an owned `CNA_MouseCursorHandle` plus `Mouse::SetCursor`, moved here from CBIND-037B4b because it cannot be mapped before a cursor handle exists. All twelve stock accessors collapse into one route taking a `CNA_MOUSE_CURSOR_STOCK_*` identity, and the handle they return is a **borrowed view**: the canonical stock cursors are process-lifetime singletons whose disposal is a deliberate no-op, so destroying the handle never frees the shared native cursor and disposing it succeeds without doing anything. The default constructor, the texture factory and both lifetime operations are mapped; the texture factory does not keep its texture alive, because the canonical one copies the pixels. Four rows are `not-applicable` and each says why: the `SDL_Cursor*` constructor and `GetSDLCursor` would put a native backend pointer in the ABI, and the move constructor and move assignment have no counterpart because a handle is the only name C has for a cursor. `InputSnapshotsSmoke.c` probes the texture-derived cursor **by behavior** — whichever documented answer the backend gives, the success path is exercised fully and the refusal path must leave the output handle invalid — and proves the stock no-op disposal by reusing an identity after disposing and releasing it. Green in all three trees (51/51) and under ASan+UBSan with leak detection on. |
| CBIND-037B4d | 27 | Complete text input | ✅ | `input_text.h` maps every `TextInputEXT` row through `cna_text_input_*` free functions, because C has no static class. All three events become owned `CNA_TextInputRegistrationHandle` values with **one** shared release route, since a registration already knows which event it came from; the subscriptions take no game handle, as the canonical events are process-wide statics. A committed code unit crosses as a `uint16_t` and an above-BMP code point arrives as two surrogate calls; the two multi-field events hand over fixed versioned infos whose UTF-8 text and candidate strings are `CNA_StringView`s borrowed only for the callback, so neither `std::string` nor `std::vector` crosses the ABI. The three `INTERNAL_On*` dispatchers become the raise routes that make the events observable without a keyboard. Two canonical quirks are preserved and asserted: composition `start`/`length` are byte offsets forwarded verbatim and `selected` is not range-checked, because the canonical dispatch checks neither. One **deliberate deviation**: an undefined type hint is refused, where the canonical conversion silently falls back to plain text. **Borrowed from CBIND-037B7:** `TextInputTypeEXT` and its nine values (10 rows) — 27 rows here, 92 left there. This closes parent `CBIND-037B4`. The suite never branches on renderer identity: it forces the unbound case to prove the null-guarded contract on every backend, then restores whatever the backend really bound — which on SDL_RENDERER is a live window where start genuinely activates text input and stop genuinely deactivates it, asserted as a relationship rather than a fixed answer. Green in all four trees (51/51) and under ASan+UBSan with leak detection on. |
| CBIND-037B5 | 80 | Complete touch and gestures | ✅ | `input_touch.h` maps the whole touch family onto the representations the C API already had. `GestureType` is a `uint32_t` bit set whose four canonical operators need **no** route — unlike the gamepad button identities these really are flags, so C composes and masks them with its own operators and every route validates against `CNA_GESTURE_TYPE_ALL`. `GestureSample` is a fixed 64-byte value rather than a handle, since it is a copyable snapshot with no identity; its eight canonical getters are plain fields and `System::TimeSpan` crosses as `int64_t` 100-nanosecond ticks, the spelling `runtime.h` and `audio.h` already use. `TouchLocation`, `TouchPanelCapabilities` and the entire `TouchCollection` mutation surface extend the **existing** fixed eight-slot `CNA_TouchState`/`CNA_TouchLocation`/`CNA_TouchCapabilities` values, so the ABI never grows a second spelling of a touch snapshot. Four canonical behaviors are preserved and asserted rather than smoothed over: equality, the hash and the text all ignore the pressure extension and the text carries **only** the position; reading an empty gesture queue throws canonically and so is refused in C rather than answered with a default sample; a raised touch event feeds gesture detection and **not** the snapshot, and is dropped outright until a display size is published, because the dispatch scales by it; and `ResetForTests` clears the display metrics and window handle even though the canonical class comment claims they survive — **the C contract follows the implementation and says so**. `CopyTo` inserts and shifts rather than overwriting, which is why the destination's element count is an argument. Three deliberate C deviations are documented: a negative maximum touch count, a pressure outside zero through one, and an append past the fixed capacity are refused rather than stored or silently dropped. Five rows are `not-applicable` with reasons: the four iterator overloads, because an iterator has no C counterpart and C indexes the fixed array directly, and the class-local `intcs` alias, which declares no operation. `InputSnapshotsSmoke.c` grew three pure validators and one in-game validator with their own return codes, proving the pressure-blind match, the insert-semantics copy, the exact text, the enqueue/read round trip, the empty-queue refusal, the released frame after a slot is cleared, and the reset really clearing the display metrics. Green in all four trees (51/51) and under ASan+UBSan with leak detection on. |
| CBIND-037B6 | 126 | Complete the haptics extension family | ✅ | `input_haptics.h` maps the whole `CNA::Input` haptics surface. This is the first input slice with **no XNA counterpart at all**, so the whole header maps a CNA-namespace surface and its routes take no `_ext` suffixes, following the `core_ext.h` precedent. It is also the first input slice to produce an **owned handle** rather than a value: `HapticDevice` becomes `CNA_HapticDeviceHandle` (`ObjectKind` 68), with the destructor/`Dispose` split `MouseCursor` established, so a caller can close a device without giving up its handle. The decision that makes the family testable at all is that a **closed device is not an error state**: the three open routes never fail for want of hardware, they hand back a real handle whose open flag reports whether anything is behind it, and every route on a closed device answers `CNA_FALSE`, zero or -1 through its output — exactly as the canonical class behaves. No verification tree has force-feedback hardware, so that path is the one actually exercised, and it is asserted rather than skipped. `HapticFeatureEXT` is a `uint32_t` bit set whose five operators need no route, C composing them itself; its canonical bit gaps (LeftRight at 11, Custom at 15, the four global capabilities at 16–19) are reproduced exactly and pinned by ABI assertions. `HapticEffectTypeEXT` and `HapticDirectionTypeEXT` are ordinal identities and an out-of-range value is refused. Two representational decisions are documented, not hidden: a **custom waveform travels beside the effect value** rather than inside it, so the 108-byte `CNA_HapticEffect` stays a plain copyable POD owning no heap, and the **device name is left out of the capability value** and read through the count/copy pair — which is why `cna_haptic_capabilities_equals` takes both names as arguments, reproducing the canonical comparison exactly instead of quietly comparing fewer fields than it does. Canonical pass-throughs are preserved: rumble strength, gain and autocenter reach the platform unvalidated, and freeing an unknown effect identifier is a successful no-op because the canonical operation reports nothing. `RunEffectEXT`'s defaulted iteration count is passed explicitly. Three rows are `not-applicable` with reasons: the `SDL_Haptic*` constructor, which would put a native backend pointer in the ABI, and the move constructor and move assignment, which have no counterpart because a handle is the only name C has for a device. The slice gets its own strict-C `HapticsSmoke.c` and `CApi_HapticsSmoke` target rather than growing `InputSnapshotsSmoke.c` further, matching its own adapter file. Green in all four trees (52/52) and under ASan+UBSan with leak detection on. |
| CBIND-037B7 | 92 | Complete the remaining input extensions | ✅ | Map the remaining `CNA::Input` joystick, sensor, device-enumeration, clipboard and power surfaces. The key-modifier, button-label, text-input-type and connection-state identities this row originally owned were borrowed into `CBIND-037B3`, `B4a` and `B4d`, which is why 92 rows remain rather than the 102 first partitioned. Split by concern into `CBIND-037B7a`–`B7b` below, because the joystick family is a device surface with its own values, snapshot and hot-plug events while the rest are small host-system queries. |
| CBIND-037B7a | 54 | Complete the raw joystick family | ✅ | `input_joystick.h` and `CnaCApiInputJoystick.cpp` map `JoystickTypeEXT`, `JoystickHatPositionEXT`, `JoystickInfoEXT`, `JoystickCapabilitiesEXT`, `JoystickStateEXT` and the `Joysticks` facade. Like haptics this is a CNA-namespace surface, so the routes take no `_ext` suffix except the two hot-plug events and the test reset, which follow their canonical member names. **The one deliberate departure from the input families' fixed-POD rule is the snapshot**, and it is the decision the slice turns on: `JoystickStateEXT` carries four heterogeneous variable-length arrays with no canonical maximum — unlike the touch panel's fixed eight slots — so a fixed value would have to invent a capacity that silently truncates a real HOTAS setup, while four independent per-array queries would answer from four different instants. `cna_joysticks_capture_state` therefore captures once into an owned `CNA_JoystickStateHandle` (`ObjectKind` 69) and each array is read with its own count/copy pair against that one instant; trackball motion is relative, so capturing consumes it, which is another reason one capture must serve all four arrays. **The hat is an identity, not a bit set** — the plan's own guess said "probably a bit set", and the canonical header says the opposite: the platform's combinable up/down and left/right bits are enumerated as the nine reachable combinations, so `RIGHT_UP` is the ordinal 5 and composing these values is wrong. That is pinned by an ABI assertion and stated in the header. The haptics **closed-device-is-not-an-error** contract carries over unchanged: an unconnected identifier answers `cna_joysticks_get_capabilities` with the canonical disconnected defaults, a power percent of -1 meaning "unknown" rather than "empty", and two empty strings, and answers `cna_joysticks_capture_state` with four empty arrays — which is the path every verification tree actually exercises, and it is asserted rather than skipped. The device name and GUID stay outside the capability value for the same reason the haptic device name does, so `cna_joystick_capabilities_equals` takes both strings alongside both values and reproduces the canonical ten-field comparison exactly; `cna_joystick_info_equals` does the same with the descriptor name. Both static multicast fields become owned registrations (`ObjectKind` 70) with one shared release route, mirroring the text-input surface, plus raise routes that invoke the same public field the platform layer invokes — no `Internal` bridge crosses the ABI. The slice gets its own strict-C `JoystickSmoke.c` and `CApi_JoystickSmoke` target, matching its own adapter file. Green in all four trees (53/53) and under ASan+UBSan with leak detection on. |
| CBIND-037B7b | 38 | Complete host sensors, device enumeration, clipboard and power | ✅ | `input_devices.h` and `CnaCApiInputDevices.cpp` map the last four `CNA::Input` extensions by reusing the shapes `CBIND-037B7a` settled rather than inventing new ones: the descriptor value with its name outside the POD and an `_equals` taking both names, the index-addressed enumeration, and the process-wide event registration (`ObjectKind` 71, one shared release route for all four events). Three decisions are worth recording. The two sensor reads follow the **availability-separate-from-the-answer** rule the gamepad sensors established, and go one step further: when the flag reports no sensor the reading output is **left exactly as the caller left it**, because that is what the canonical query does with its reference — the test proves it by pre-filling sentinel components and asserting they survive. `CNA_InputDeviceInfo` carries a **`uint64_t`** identifier where the sensor and joystick descriptors carry `uint32_t`, because a touch-device identifier is 64-bit natively; the test round-trips a value above the 32-bit range so a narrowing conversion could not pass unnoticed. And `cna_clipboard_set_text` **reports that the request was made, not that it succeeded**: the canonical setter returns nothing, so there is no platform outcome to forward and this ABI does not invent one — a headless session or a gesture-gated browser may ignore the write. The clipboard is process-external state the suite does not own, so its test captures the pre-existing content, asserts a *relationship* (if the write took effect, the read must return exactly those bytes; the presence flag must agree with a non-empty read in both directions), proves the empty and buffer-too-small cases only on a platform that actually stored the text, and restores the original content. One strict-C `InputDevicesSmoke.c` covers all four families, driving the three device enumerations through a single shared validator so the protocol is proven identically for mice, keyboards and touch devices. Green in all four trees (54/54) and under ASan+UBSan with leak detection on. **This closes parent `CBIND-037B7`, parent `CBIND-037B` and the whole `input` module**: 834 implemented, 27 not applicable, no partial and no planned row left. |

#### CBIND-037D devices implementation slices

The 289 `devices` and `devices-ext` rows split by what can be tested together: the reading values
are pure PODs and need nothing, the sensor devices produce them and own the events and exception
types, and the two `CNA::Devices` groups are independent system services. **The owner decision of
2026-08-15 applies to this whole slice:** `cmake-build-binding-sdlrenderer` and
`cmake-build-binding-asan` were reconfigured with `-DCNA_DEVICES=ON` so the `#ifdef CNA_DEVICES`
half of `devices-ext` is genuinely exercised, while `headless` and `software` stay OFF and prove
the compiled-out contract. Both states must stay green.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-037D1 | 70 | Establish sensor readings, timestamps and state | ✅ | `sensors.h` and `CnaCApiSensors.cpp` map `SensorState`, `ISensorReading` and all five reading types as fixed values. The slice settles the ABI's **second** point-in-time form: `CNA_DateTimeOffset` is two 100-nanosecond tick counts — local time from **0001-01-01** plus the UTC offset — because that is the canonical runtime type's own base, exactly as the picture date uses the Unix epoch because *its* canonical type does. The reading interface becomes the `timestamp` field every reading carries rather than an abstract type C cannot use, and its virtual destructor is the one `not-applicable` row. Three canonical behaviors are preserved rather than tidied: **each constructor keeps its own argument order** even though the five disagree (the accelerometer takes the timestamp first, the gyroscope the rate first, the compass the true heading last) — normalizing them would make the C API easier to remember and harder to check; equality pairs the values **with** the timestamp; and the text conversions carry only part of each reading, with the motion reading's omitting the attitude and rotation rate a reader would expect. All six state identities are exposed, including the two the canonical header records as currently unreachable, because an identity is not a claim that something produces it. `cna_c_api` gained its `cna_devices` link edge here. Strict-C `SensorValuesSmoke.c` needs no game at all; green in all four trees (59/59) and under ASan+UBSan with leak detection on. |
| CBIND-037D2a | 80 | Map the motion sensors, their failures and the test-support surface | ✅ | `Accelerometer` and `Gyroscope` are owned handles in `sensors.h`; `CnaCApiSensors.cpp` carries them, and the two exception types become one route rather than a type. The common base is a **class template**, so C repeats its contract per sensor instead of modeling a base — the reading-changed callback delivers the reading itself, because the event-argument wrapper adds nothing. Three canonical behaviors are reported, not smoothed: reading an unsupported sensor's value fails `INVALID_STATE` (the canonical property throws rather than defaulting), a **second disposal is refused** where every other disposable in this ABI is idempotent, and there is no disposal query at all because the canonical flag is protected — the disposed state is observed through the refusals. `SensorFailedException`'s error id reaches C exactly as the network join error does: recorded per thread by the barrier, read back with `cna_sensors_get_last_error_id_ext`. The **test-support surface is mapped deliberately** — no verification machine has motion sensors, so `set_supported_for_tests_ext` plus `inject_synthetic_update_ext` are what let a C consumer reach the supported path and the real dispatch chain; the injector takes platform units, so 9.80665 m/s² reads back as 1 g. Strict-C `SensorDeviceSmoke.c` covers both dispatch paths, the detaching registration, the double-start failure and its error id, the disposal hook firing once, and every post-disposal and stale-handle refusal; green in all four trees (60/60) and under ASan+UBSan with leak detection on. |
| CBIND-037D2b | 46 | Complete the remaining sensors and the reading events | ⬜ | Map `Compass` and `Motion` on the shape `CBIND-037D2a` settled, plus `AccelerometerReadingEventArgs`, `CalibrationEventArgs` and the `SensorReadingEventArgs<T>` template with its four concrete instantiations, and wire `Accelerometer::ReadingChanged` — the deliberately obsolete legacy event `D2a` left planned so both reading events land with the types they deliver. Expect `Compass` to be an honest `NotSupported` stub everywhere but Android, so its unsupported path is the one the trees exercise; expect `Compass::Calibrate` and its calibration event to have no way to fire here. Decide whether the event-args types earn C values of their own or collapse into their payload the way `D2a`'s did — the answer should be the same for both, and the reason belongs in `DEVICES.md`. |
| CBIND-037D3 | 69 | Complete VibrateController and the CNA system services | ⬜ | Map `VibrateController`, `Clipboard`, `PowerState`/`PowerInfo`, `Locale`/`LocaleInfo`, `DisplayInfo`, `SystemInfo`, `UrlLauncher`, `MessageBox`/`MessageBoxType`, `FileDialog`/`FileDialogFilter` and `SystemTray`. **Every `CNA::Devices` header is `#ifdef CNA_DEVICES`**, so each route needs the `CnaCApiGraphicsExt.cpp` shape: exported in both build states, reporting `NOT_SUPPORTED` when the extension layer is compiled out. Note the clipboard here is a *different* type from the input module's `CNA::Input::Clipboard`, which `CBIND-037B7b` already mapped — check whether they wrap the same platform state before naming the routes. |
| CBIND-037D4 | 24 | Complete the camera extension | ⬜ | Map `Camera`, `CameraState`, `CameraPosition` and `CameraDeviceInfo` under the same `CNA_DEVICES` rule. A camera produces frames, so decide whether they reuse the texture contract `CBIND-037C7` established or a raw byte transfer, and expect no capture hardware in any verification tree — the unavailable path is the one that will be exercised. |

#### CBIND-037C media implementation slices

The 325 `media` rows split by what each part needs to exist: the identities and standalone values
first, then the song that everything plays, then the library entities and their collections, then
the library that owns them, then the player that consumes them, and video last because it composes
the graphics surface as well. `cna_c_api` gained its `cna_media` link edge in `CBIND-037C1` — the
module list stays exactly what the C API adapts.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-037C1 | 25 | Establish media identities, visualization and sources | ✅ | `media.h` and `CnaCApiMedia.cpp` map `MediaState`, `MediaSourceType`, `VideoSoundtrackType`, `VisualizationData` and `MediaSource`. Two decisions carry the slice. **`CNA_MediaSourceType` keeps its canonical 0/4 gap** rather than being renumbered into a dense range, so it deliberately has no `MAXIMUM` and consumers validate membership of the two defined values instead of an upper bound — the same rule that kept `CNA_LOG_LEVEL_EXPERIMENT` at 100. And **the canonical source enumeration's ownership never crosses the ABI**: `MediaSource::GetAvailableMediaSources` allocates its sources with `new` and hands back raw pointers its caller must free, so each C route enumerates, reads the one source it was asked about and destroys the whole list before returning; an index is a point-in-time value with nothing to release, and the sanitizer tree with leak detection is what proves it rather than a comment claiming it. `ToString` needs no route of its own because the canonical implementation returns the display name unchanged, and the media-source type name is addressed by index because the canonical member is an instance method on a type not constructible from outside the library. `CNA_VisualizationData` is a fixed 2,056-byte value rather than a handle, since both canonical buffers are fixed at 256 floats and the canonical type exposes them both as fields and through getters — one value is both. Strict-C `MediaSmoke.c` plus C and C++ ABI assertions; green in all four trees (55/55) and under ASan+UBSan with leak detection on. |
| CBIND-037C2 | 37 | Complete Song and SongCollection | ✅ | `media.h` grows an owned `CNA_SongHandle` (`ObjectKind` 72) and `CNA_SongCollectionHandle` (73). **Several handles share one song**: the resource is reference-counted, so releasing one handle never destroys a song a collection still holds — which is what lets `cna_song_collection_create` *retain* every song it was given, where the canonical collection merely stores non-owning pointers a released C handle would have dangled. Three canonical behaviors are preserved rather than tidied, and the first is a header-contradicts-implementation case like `TouchPanel::ResetForTests`: **an omitted song name stays empty** even though the constructor's own documentation claims it defaults to the file name; **equality and the hash come from the file path**, so two independently created songs over one file compare equal and hash equal — a deliberate CNA improvement over FNA's identity-based hash, kept rather than "fixed"; and `getIsRated` is **not** "rating is nonzero", because both tag formats reserve zero for unrated. `ToString` needs no route of its own (it returns the display name unchanged), a missing file surfaces as `CNA_RESULT_IO` through the canonical file-not-found exception, and a non-`file` URI scheme as `CNA_RESULT_INVALID_STATE`. Canonical collection disposal **empties** the collection, so its count drops to zero and every index is refused while the songs survive. Seven rows are `not-applicable` with reasons: the `MediaLibrary` friend declaration, and the collection's iterator pair with its two aliases. **Re-partitioned:** `getAlbumProperty`, `getArtistProperty` and `getGenreProperty` move to `CBIND-037C3` (37 rows here, 105 there), because they return library-owned entities whose handles do not exist yet. `MediaSmoke.c` builds its fixture files through the storage API — the only portable way a strict-C17 test can obtain a real absolute path — with one non-ASCII UTF-8 file name. Green in all four trees (55/55) and under ASan+UBSan with leak detection on. |
| CBIND-037C3 | 117 | Complete the library catalog: MediaLibrary, albums, artists, genres, playlists | ✅ | `media_library.h`, `CnaCApiMediaLibrary.cpp` and `MediaLibrarySmoke.c` map `MediaLibrary` and the four entity families with their collections (`ObjectKind` 74–82). **Re-partitioned on arrival:** `MediaLibrary` moved here from `CBIND-037C5` and its six picture rows moved to `CBIND-037C4`, because none of the entity types is constructible from outside the library — a slice that mapped them without it could not have produced a single testable object. The shape decision is that **everything except the library is a borrowed view holding a reference to its library**, so releasing the library handle first is safe and there is no parent-before-child rule to remember; the four structurally identical collection types therefore share one C shape rather than four. Album equality is **not** the name alone: names collide across artists, so the canonical comparison pairs name with artist. Optional entities — an album's artist and genre, a song's album, artist and genre — follow the availability-separate-from-the-answer rule. **No stream crosses the ABI:** the canonical art members hand back a caller-owned stream, so C reads it to the end and destroys it inside the call and the image crosses as bytes; the thumbnail is the same image, which is canonical rather than a C limitation. The sanitizer tree earned its keep here — it proved that `MediaLibrary(MediaSource*)` **borrows** its argument (it copies the kind and name into an object of its own) rather than adopting it, so the C route destroys every enumerated source before returning. Twenty-five rows are `not-applicable`: the `Album::MediaLibrary` friend declaration and the four collections' iterator pairs and aliases. The test points SDL's user-folder lookup at a generated fixture through `XDG_CONFIG_HOME`, so the scanned library is **deterministic** — two tag-only MP3 files sharing an artist, album and genre plus a folder cover whose exact bytes the art routes must return — instead of depending on whatever music the host holds, and no real user directory is read or written. Green in all four trees (56/56) and under ASan+UBSan with leak detection on. |
| CBIND-037C4 | 60 | Complete pictures, picture albums and the library's picture surface | ✅ | `media_library.h` grows `CNA_PictureHandle`, `CNA_PictureAlbumHandle` and their two collections (`ObjectKind` 83–86), plus the six `MediaLibrary` picture rows re-partitioned in from `CBIND-037C3`. Two shapes are new. **The picture-album tree is the only tree in the media family**, and the root's absent parent is what makes it walkable: `cna_picture_album_get_parent` reports availability rather than failing, so a caller climbs until the flag turns false; `cna_media_library_get_root_picture_album` answers the same way, because a device with no readable picture location has no tree at all. And **a picture's date is the ABI's first point in time**: durations elsewhere are 100-nanosecond ticks from zero, so the date uses the same tick counted from the Unix epoch — the canonical clock's own epoch — rather than inventing a second time unit. Everything else reuses shapes already settled: image and thumbnail bytes follow the album-art contract (the canonical caller-owned stream is read to its end and destroyed inside the call, so no stream enters the ABI, and the thumbnail is the same image), and the collections are the same six-route shape the music collections use. `cna_media_library_save_picture_from_stream` takes a **storage stream handle**, since a storage stream is the only byte source this ABI owns — the same decision `content_readers.h` made — borrowed for the call and left the caller's to close. Twelve rows are `not-applicable`: the two collections' iterator pairs and aliases. The `CBIND-037C3` fixture is extended with a one-pixel BMP, so the picture side is as deterministic as the music side; the suite deletes the picture it saves so repeated runs start from the same state. Green in all four trees (56/56) and under ASan+UBSan with leak detection on. |
| CBIND-037C5 | 0 | Complete MediaLibrary | ✅ | **Absorbed into `CBIND-037C3`** (music surface) and `CBIND-037C4` (picture surface). `MediaLibrary` could not be a slice of its own in either direction: its members return the entity collections, and none of those entities can be obtained without it. |
| CBIND-037C6 | 44 | Complete MediaPlayer and MediaQueue | ✅ | `media_player.h` and `CnaCApiMediaPlayer.cpp` map the static `MediaPlayer` as free game-scoped routes and the queue as a **view of one process-lifetime object** (`ObjectKind` 87, registrations 88). Two canonical behaviors are preserved rather than tightened — the volume setter clamps instead of refusing, and the indexed `Play` overload is not range-checked — and two deviations are forced by ownership and documented: **a queue entry crosses as an independently owned copy** rather than a borrowed view, because the canonical queue destroys its entries on every clear (which every play route does) and a borrowed handle would dangle, and **`cna_media_queue_add` appends a copy** because the canonical `Add` adopts the pointer it is given, which C cannot do without leaving the caller a stale handle. Both copies carry the same file and name, so they compare equal to the original — and appending a copy is exactly what the canonical player itself does when it enqueues a song. Four rows are `not-applicable` with a stated limitation: the queue's constructor, destructor and move operations, since exactly one queue exists and C never constructs, moves or destroys it. The test asserts the playback transitions **as a relationship**, because whether `play` actually starts playing depends on the platform's ability to decode the fixture rather than on the C API — the paused/playing round trip is asserted when playback began and the no-op contract otherwise. Green in all four trees (57/57) and under ASan+UBSan with leak detection on. |
| CBIND-037C7 | 42 | Complete Video and VideoPlayer | ✅ | `video.h` and `CnaCApiVideo.cpp` map both types (`ObjectKind` 89–90) and **close the media module**. The slice's one hard problem is the frame texture, and it is solved by lifetime rather than by copying: the player owns and replaces its texture, so `cna_video_player_get_texture` hands back a borrowed `CNA_Texture2DHandle` that the C layer **invalidates on the next call to that player** — any later route, including another `get_texture`, releases it, so a stale frame fails with `CNA_RESULT_INVALID_HANDLE` instead of touching freed memory. The graphics device is reported as **presence only**, because a borrowed device handle is valid solely inside the callback that produced it. Three canonical behaviors are reported rather than corrected, and two of them were established by running the code rather than reading the header: an undecodable file leaves the metadata zeroed and, on play, leaves the player stopped **with its video cleared**, so `get_video` answers `CNA_FALSE`; and `FromUriEXT` **does not parse URIs** at all — unlike the song factory it forwards the text to the file constructor, so an `http:` string is just a missing path. `VideoInfo` gains a real producer (`cna_video_get_info`) instead of being exposed as a type nothing can fill. One row is `not-applicable`: the test-access friend declaration. The test writes its own non-video fixture rather than depending on another suite's files — a dependency that first showed up as a parallel-ctest failure. Green in all four trees (58/58) and under ASan+UBSan with leak detection on. **`media` now records 276 implemented, 0 partial, 0 planned and 52 not applicable**, closing parent `CBIND-037C`. |

#### CBIND-036B content implementation slices

The 104 `content` rows split once, at the boundary between the manager a C consumer actually drives
and the XNB reader pipeline that only C++ type readers can participate in. The manager side lands
first because the reader side needs its stream and type-reader contracts already settled.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-036B1 | 40 | Complete the content manager, manifest values and load failures | ✅ | `content.h` and `CnaCApiContent.cpp` map every `ContentManager.hpp`, `ContentManifestEntry.hpp` and `ContentLoadException.hpp` row: resolved asset path and normalized cache key count/copy, built-in loader registration, service-provider presence, graphics-device get/set validated by borrowed-handle re-validation and pointer identity, the manifest and `.xnb` reader-usage snapshots as fixed PODs plus count/indexed copy, and typed `Load<Texture2D>`, `Load<TextureCube>` and `Load<SoundEffect>` routes returning independently owned handles. `System::IServiceProvider` stays a hard ABI boundary, so the two service-provider constructors and `getServiceProviderProperty` remain documented `partial`; `RegisterTypeReader<T>`, `RegisterCnjLoader<T>`, `CnjLoaderFn<T>` and the `log` alias are `not-applicable` because C cannot name an arbitrary C++ type. `ContentSmoke.c` builds its own content root through the storage API so the manifest scan is deterministic, and runs green in all three trees (48/48). |
| CBIND-036B2 | 64 | Complete the XNB reader pipeline | ✅ | `content_readers.h` and `CnaCApiContentReaders.cpp` map every remaining `content` row. An owned `CNA_ContentReaderHandle` is built over an owned storage stream plus an optional manager handle, both borrowed through new adapter records so neither `System::IO::Stream` nor `ContentManager` crosses the ABI; the borrow blocks closing the stream, and destroying the reader closes it exactly as the canonical binary-reader base does. The reader exposes asset name/version/platform, all six fixed-value reads plus the bounding sphere, the type-reader-table and shared-resource passes, both bounds checks and a capacity-checked exact-byte read that consumes nothing when it refuses. An owned `CNA_ContentTypeReaderHandle` comes from the static registry or from the known-unsupported placeholder factory and carries the target type name, type version, version support, in-place-deserialization capability and initialization; the registry itself is three free functions because the canonical manager holds no state. Type erasure is where the mapping stops and that is recorded, not papered over: the two untyped read routes are `partial` because a type-erased C++ object has no C representation, and the typed `ReadObject<T>`/`ReadRawObject<T>`/`ReadSharedResource<T>`/`ReadAsset<T>`/`ReadExternalReference<T>` templates, `ContentTypeReader<T>`, `LooseFileContentTypeReader<T>`, `AddTypeCreator` and the protected/detail declarations are `not-applicable`. `ContentLoadException` gained a central boundary conversion to `CNA_RESULT_IO`. Strict-C `ContentReaderSmoke.c` builds a compiled-asset fixture through the storage API and runs green in all three trees (49/49). The `content` module now has no planned row left. |

#### CBIND-035F device and draw-submission implementation slices

The 317 rows owned by CBIND-035F are partitioned by dependency boundary. Value and identity
contracts land first, then device lifetime/state, then the collections, frame-control, binding and
draw routes that consume them; the renderer-neutral `graphics-ext` post-process family is last
because it builds on the completed effect and texture contracts.

| # | Rows | Task | Status | Acceptance criteria |
|---|---:|---|---|---|
| CBIND-035F1 | 49 | Establish device values and identities | ✅ | `graphics_device.h` maps the complete `Viewport` header through a fixed 24-byte POD whose six public fields are its whole property set, plus construction, aspect ratio, bounds get/set, title-safe area, project/unproject and exact UTF-8 string count/copy. `CNA_ClearOptions`, `CNA_GraphicsDeviceStatus` and `CNA_Unsupported3DGraphicsCallBehavior` freeze their native ordinals, and the native ClearOptions/SpriteEffects operator overloads collapse to C's own bitwise operators on the fixed-width aliases. Source-side static assertions bind every identity to its native ordinal; strict-C `GraphicsDeviceSmoke.c` covers all three constructors, both zero-dimension aspect cases, depth-range scaling, clip-space corners, identity and perspective project/unproject round trips, exact strings, capacity atomicity and null arguments under HEADLESS and SDL_RENDERER, with C/C++ ABI layout assertions. |
| CBIND-035F2 | 51 | Complete device lifetime, state, events and service surface | ✅ | `graphics_device.h` maps device disposal state, status, adapter index, profile, scissor/viewport/blend-factor/multisample-mask/reference-stencil get and set, exact UTF-8 type name and an explicit `NOT_SUPPORTED` dispose result that names the game's ownership of the canonical device. All six events become owned subscriptions with fixed `CNA_ResourceCreatedEventInfo`/`CNA_ResourceDestroyedEventInfo` payloads; a created resource and a destroyed resource's tag are honestly reported as presence only because the canonical events expose a partially constructed object and caller-owned native state. Game destruction invalidates live subscriptions after the device raises Disposing, so a subscriber observes it and its handle stays releasable. The shared exception firewall converts `DeviceLostException`/`DeviceNotResetException` to `INVALID_STATE` and `NoSuitableGraphicsDeviceException` to `NOT_SUPPORTED` with their exact messages. Strict-C HEADLESS and SDL_RENDERER tests cover every route, real resource events, defaults, non-finite viewport rejection, capacity atomicity, stale handles and post-destruction release; the adapter test covers all three exception conversions and C/C++ assertions freeze both payload layouts. Three GraphicsDevice friend declarations and the four service-level `IGraphicsDeviceService` events are recorded as not applicable and partial respectively. |
| CBIND-035F3 | 8 | Complete texture and vertex-texture collections | ✅ | `graphics_device.h` maps `TextureCollection` and both device collection properties through stage-addressed slot operations: `CNA_TEXTURE_COLLECTION_MAX_TEXTURES` is asserted against the native constant, the indexing operator becomes a versioned `CNA_TextureSlotInfo` read, the call operator becomes a validated bind and `RemoveDisposedTexture` becomes an explicit unbind. No native vector, collection reference or raw `Texture*` crosses the ABI, and a slot filled by canonical CNA code reports as bound with an invalid handle. Strict-C HEADLESS and SDL_RENDERER tests cover both stages across all 16 slots, bind/read/unbind round trips, self-unbinding on texture destruction, render-target rejection where the backend binds one, and invalid stage/slot/structure/handle failures, with C/C++ ABI layout assertions. |
| CBIND-035F4 | 21 | Complete frame control and buffer binding | ✅ | `graphics_device.h` maps all remaining Clear overloads, Present, all four Reset overloads, both remaining `GetBackBufferData` windows and the complete vertex/index binding set. The reference and pointer adapter overloads collapse into one nullable-index route that preserves the renderer-private window handle; the nullable readback rectangle becomes an explicit flag plus value; and an element count smaller than the selected region is decided in C as `BUFFER_TOO_SMALL` rather than surfacing as a generic native failure. The four canonical index-buffer accessors collapse to one validated get/set pair, multi-binding application is atomic on rejection, and reads report the owning C handle or an invalid handle for a binding applied by canonical CNA code. Strict-C HEADLESS and SDL_RENDERER tests cover every route, non-finite and unknown-bit rejection, observed reset event pairs, untouched destination bytes and honest backend refusal, with C/C++ ABI layout assertions. |
| CBIND-035F5 | 49 | Complete draw submission and device extensions | ✅ | `graphics_device.h` maps all three buffered draw routes, the canonical topology vertex-count helper and all twenty-nine user-primitive overloads through two calls whose versioned `CNA_UserPrimitives`/`CNA_UserIndices` descriptors carry the vertex-source, declaration and index-width dimensions. Built-in vertex sources are converted before submission because those structures embed a polymorphic Color, and a raw stream always requires a declaration since the declaration-less canonical raw overloads read native objects. All CNAEXT device helpers are mapped; the resource-notification hooks and `GetRenderer` are documented as having no C route because each carries a native object, with their observable effects reachable through the event subscriptions, tracked-resource count and renderer queries. Draw routes and pipeline-state toggles refuse a backend without 3D support as `NOT_SUPPORTED`, and an assigned current effect is kept alive by the C API because the device stores a borrowed pointer. Strict-C HEADLESS and SDL_RENDERER tests cover every route, validation, capability-accurate refusal and lifetime, with C/C++ ABI layout assertions. This leaves no planned GraphicsDevice.hpp row. |
| CBIND-035F6 | 21 | Complete SpriteBatch text routes and occlusion queries | ✅ | `graphics.h` collapses all six `DrawString` overloads into one versioned `CNA_SpriteTextCommand`, because both native text types are copied before layout and the parameter shapes differ only in which transform fields stay at their defaults; `CNA_SpriteMeshEXT` maps `DrawMeshEXT` with converted colors and positions, since the native Color carries a vtable, and reports `NOT_SUPPORTED` on a renderer without mesh submission. Exact type-name count/copy is added, and both non-C constructors are documented: the default one produces a deviceless batch and the other takes a renderer-private object. `graphics_device.h` owns `OcclusionQuery` through a game-child handle with capability-gated creation, begin/end, completeness, pixel count and live-renderer state; the handle is a full graphics resource, so the generic `cna_graphics_resource_*` routes cover its protected disposal and type name. Strict-C HEADLESS and SDL_RENDERER tests cover every route, malformed and foreign-handle rejection, retained font atlases and honest backend refusal, with C/C++ ABI layout assertions. |
| CBIND-035F7 | 118 | Complete graphics-ext post-process and pipeline settings | ✅ | `graphics_ext.h` maps all seven extension identity enumerations at their native ordinals, both settings bags as fixed-layout `CNA_PbrMaterial`/`CNA_RenderPipelineSettings` PODs with canonical-default initializers, and the three post-process effects. CRTEffect and DepthEffect become ordinary owned `CNA_EffectHandle` values so the whole `cna_effect_*` contract covers their inherited ShaderEffect behavior; AsciiPostProcessEffect gets its own handle because it is not a shader Effect, and its two Draw overloads collapse into one nullable-rectangle call. Because the extended layer is an opt-in build option, every declaration exists in every build and the effect routes report `NOT_SUPPORTED` when it is absent, so no exported symbol changes shape with the option. Strict-C tests cover the complete unavailable contract under HEADLESS without the layer and, under SDL_RENDERER with it, real creation of all three effects, exact canonical defaults, native clamping, every mode identity, cross-type handle refusal, cell-size validation and a real ASCII draw with a measured glyph grid, plus C/C++ ABI layout assertions. This closes parent CBIND-035F: no planned CBIND-035 inventory row remains. |

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

**Snapshot (2026-08-15, after `CBIND-037D1`):** 414 headers / 6,415 symbols —
**4,844 implemented, 30 partial, 1,362 planned, 179 not applicable.**
Regenerate or verify with `python3 tools/c-api/generate_coverage_inventory.py --write|--check`.

### What is closed

| Scope | State |
|---|---|
| `CBIND-000`–`CBIND-034` | ✅ all |
| `CBIND-035` (math, geometry, textures, effects, models, device and draw submission) | ✅ closed by `CBIND-035G` |
| `CBIND-036` (storage, content, networking, fake-async) | ✅ closed by `CBIND-036E5` |
| `CBIND-037A` core, `CBIND-037B` **the whole input module** (gamepad, keyboard, mouse, cursor, text input, touch, haptics, joysticks, host devices) | ✅ |
| `CBIND-037C` **the whole media module** (identities, songs, the library catalog, pictures, playback, video) | ✅ |

**Six modules now have no planned row left: `storage`, `content`, `net`, `core`, `input`, `media`.**

### What remains

Everything still open belongs to `CBIND-037` (1,362 rows), the B7 hardening phase
(`CBIND-038`–`042`) and the final close (`CBIND-044`). The CI coverage gate `CBIND-043` is already
done and is not waiting on `CBIND-037`.
`CBIND-037` is partitioned into seven module-sized slices; work them in this order, because each
later one composes the earlier ones:

| Order | Slice | Rows left | Note |
|---:|---|---:|---|
| 1 | `CBIND-037D` devices and devices-ext | 219 (after `D1`) | sub-partitioned into `D1`–`D4`; `D1` is done, `D2` sensor devices is next. `sdlrenderer` and `asan` now build with `CNA_DEVICES=ON` |
| 2 | `CBIND-037E` runtime | 273 | `Game`, `GameWindow`, `GraphicsDeviceManager`, components, services |
| 3 | `CBIND-037F` audio | 205 | remaining SoundEffect, dynamic instances, microphone, XACT, 3D |
| 4 | `CBIND-037G` gamer services | 665 | largest; builds on the signed-in-gamer surface `CBIND-036E2`/`E3` already borrowed |

`CBIND-043` is done — the matrix is a gate in both CTest and CI, so an unmapped public symbol now
fails a build rather than merely showing up in a report. After `CBIND-037` closes: `CBIND-038`–`042`,
then `CBIND-044`.

### Judgment calls that produced the closed slices

The mechanics — which files, which commands — are in *The loop for one slice* in the handoff
below. These are the decisions that mechanics cannot make for you:

1. **Read the canonical `.cpp`, not only the header.** Several slices turned on behavior only the
   implementation reveals: a square clamp, an epsilon comparison, a silently dropped key, a
   deliberately no-op disposal, a hash that ignores half the fields.
2. **Prefer the representation the ABI already has** over a second spelling of the same data. The
   gamepad button set and directional pad reuse one mask; the thumbstick and trigger values are the
   two halves of a block the snapshot already carried, and an ABI assertion proves it.
3. **Collapse only what the canonical implementation itself collapses.** Eleven named button
   getters become one route because each is the same masked test — not because eleven routes felt
   verbose.
4. **When C must differ, deviate deliberately and write it down**, in the header, the coverage rule
   and here. When the canonical behavior is merely odd, preserve it and test it.
5. **Check the implemented delta equals the slice's row count** after regenerating coverage. That
   check has already caught a rule silently claiming another slice's rows.

### Recorded deviations and re-partitions

Both kinds are recorded rather than smoothed over; a new context should not "fix" them:

- **Deliberate C-layer deviations:** out-of-range keyboard keys, unchecked
  `NetworkSessionProperties` indices, undefined text-input type hints, negative maximum touch
  counts, touch pressures outside zero through one, and touch appends past the fixed snapshot
  capacity are all refused instead of silently dropped, left undefined or quietly falling back to a
  different value; a gamepad snapshot carries one button mask, so a supplied directional pad is
  merged into it.
- **Re-partitions:** `LocalNetworkGamer` D→E; three `Create` overloads E4→E2; a minimum
  gamer-services surface borrowed into E2/E3; `GamePad::GetCapabilities` B3→B1;
  `Mouse::SetCursor` B4b→B4c; the CNA::Input identity enumerations borrowed from B7 into B3, B4a
  and B4d.

### Historical narrative (append-only, oldest first)

The
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
snapshot becomes 1,803 implemented, 21 partial, 4,521 planned and 70 not applicable. CBIND-035B7
completes all 132 remaining PackedVector/HalfTypeHelper/interface rows and closes parent B; the
current snapshot is 1,935 implemented, 21 partial, 4,389 planned and 70 not applicable, with
CBIND-035C texture, buffer and vertex-resource coverage next. CBIND-035C1 maps 104 built-in
vertex-value, VertexElement-operation and IVertexType rows through fixed PODs and type-tagged
operations; the snapshot is now 2,039 implemented, 21 partial, 4,285 planned and 70 not applicable,
with CBIND-035C2 vertex declarations and bindings next. CBIND-035C2 then maps its 14 rows through
owned declaration handles and a fixed binding descriptor; the snapshot is 2,053 implemented,
21 partial, 4,271 planned and 70 not applicable. CBIND-035C3 maps the 21-row common
GraphicsResource contract through generic validated handles, exact UTF-8 names/strings, C-owned
tag tokens, callback-scoped device identity and explicit disposal subscriptions; the snapshot is
now 2,074 implemented, 21 partial, 4,250 planned and 70 not applicable. CBIND-035C4 completes the
134 previously unfinished Texture/Texture2D rows and upgrades the two inherited partial Texture
properties through the generic typed transfer, image-memory/file and storage-safe handle contract;
the snapshot is now 2,210 implemented, 19 partial, 4,116 planned and 70 not applicable.
CBIND-035C5 then maps all 40 Texture3D/TextureCube rows through owned handles, explicit
volume/face/mip/region transfer descriptors and copied DDS input; the snapshot is now 2,250
implemented, 19 partial, 4,076 planned and 70 not applicable. CBIND-035C6 maps all 57
VertexBuffer/DynamicVertexBuffer rows through owned handles, copied declarations, typed/raw
transfers and ContentLost registration; the snapshot is now 2,307 implemented, 19 partial, 4,019
planned and 70 not applicable. CBIND-035C7 maps all 32 IndexBuffer/DynamicIndexBuffer rows through
owned handles, both index widths, caller-window transfers and ContentLost registration, closing
parent CBIND-035C; the snapshot is now 2,339 implemented, 19 partial, 3,987 planned and 70 not
applicable. CBIND-035D is partitioned into nine dependency-ordered slices; CBIND-035D1 maps all 17
EffectParameterClass/EffectParameterType rows through stable fixed-width identities, bringing the
snapshot to 2,356 implemented, 19 partial, 3,970 planned and 70 not applicable, with CBIND-035D2
effect annotations next. CBIND-035D2 maps all 30 annotation/collection rows through owned copied
handles, typed getters and count/index/name snapshot operations; the snapshot is now 2,386
implemented, 19 partial, 3,940 planned and 70 not applicable. CBIND-035D3 maps all 84
EffectParameter/Collection rows through stable mutable handles, tagged scalar/array/string/texture
operations and nested count/index/name/semantic views; the snapshot is now 2,470 implemented,
19 partial, 3,856 planned and 70 not applicable. CBIND-035D4 maps all 67 technique/pass/collection
rows through stable handles, identities, nested views and canonical Apply dispatch; the snapshot is
now 2,537 implemented, 19 partial, 3,789 planned and 70 not applicable. CBIND-035D5 maps its 68
callable Effect/EffectMaterial/ShaderEffect/SpriteEffect rows through owned game-child handles,
clones, current collections, exact strings, uniforms, textures and matrices; its two deleted copy
operations remain not applicable. The snapshot is now 2,605 implemented, 19 partial, 3,721 planned
and 70 not applicable at the end of CBIND-035D5.
CBIND-035D6 maps all 90 BasicEffect, DirectionalLight and effect-interface rows through reusable
matrix/fog/light operations, stable nested light handles, complete material state and retained
Texture2D assignments. The snapshot is now 2,695 implemented, 19 partial, 3,631 planned and 70 not
applicable at the end of CBIND-035D6.
CBIND-035D7 maps all 114 AlphaTestEffect, DualTextureEffect and EnvironmentMapEffect rows through
shared effect interfaces plus complete concrete state and clone-aware retained Texture2D/TextureCube
slots. The snapshot is now 2,809 implemented, 19 partial, 3,517 planned and 70 not applicable
at the end of CBIND-035D7.
CBIND-035D8 maps all 52 SkinnedEffect rows through complete material/lighting/fog/texture state,
the fixed 72-bone maximum and bounded copied palette operations. The snapshot is now 2,861
implemented, 19 partial, 3,465 planned and 70 not applicable.
CBIND-035D9 maps all 129 ColorMatrixEffect, PbrEffect and SkinnedPbrEffect extension rows through
finite fixed-layout color transforms, shared PBR material/interface operations, five clone-aware
retained texture slots and bounded 72-bone palette transfer. The snapshot is now 2,990
implemented, 19 partial, 3,336 planned and 70 not applicable; parent CBIND-035D is complete and
CBIND-035E model, mesh and animation coverage is next.
CBIND-035E is partitioned into seven dependency-ordered slices. CBIND-035E1 maps all 23
ModelBone/ModelBoneCollection rows through stable hierarchy nodes and live collection views with
cycle prevention and no dangling parent exposure. The snapshot is now 3,013 implemented,
19 partial, 3,313 planned and 70 not applicable. CBIND-035E2 maps all 28 ModelMeshPart and
ModelMeshPartCollection rows through stable shared parts, retained graphics-resource associations,
opaque C tags and count/index snapshot aliases. The snapshot is now 3,041 implemented, 19 partial,
3,285 planned and 70 not applicable. CBIND-035E3 maps all 38 ModelMesh, ModelMeshCollection and
ModelEffectCollection rows through owned game-child meshes, live part/effect views and retained
mesh snapshots with transitive lifetime. The snapshot is now 3,079 implemented, 19 partial,
3,247 planned and 70 not applicable. CBIND-035E4 maps all 14 Model rows through owned aggregate
handles, retained bone/mesh/root views, opaque tags, C-native owned-resource callbacks, bulk local/
absolute transforms and capability-gated Draw. The snapshot is now 3,093 implemented, 19 partial,
3,233 planned and 70 not applicable, with CBIND-035E5 morph-target extensions next.
CBIND-035E5 maps all 20 MorphTargetEXT rows through fixed copied descriptors, an owned validated
data handle, atomic nested-field copies, mutable weights/tracks, blend/evaluation operations and
retained ModelMeshPart upload. The snapshot is now 3,113 implemented, 19 partial, 3,213 planned
and 70 not applicable. CBIND-035E6 maps all 34 applicable SkinnedModelEXT rows through deep-copied
skeleton/clip descriptors, deterministic bulk access, native transform sampling and stable retained
GPU-resource sidecars; its deleted copy constructor and copy assignment remain the two established
not-applicable rows. The snapshot is now 3,147 implemented, 19 partial, 3,179 planned and 70 not
applicable, with CBIND-035E7 SkinningData and AnimationPlayer next.

CBIND-035E7 maps all 19 AnimationPlayer.hpp rows through owned copied SkinningData and retained
AnimationPlayer handles, deterministic clip lookup, finite-seconds update controls and atomic
local/world/skin transform copies. The snapshot is now 3,166 implemented, 19 partial, 3,160
planned and 70 not applicable; parent CBIND-035E is complete and CBIND-035F is next.

CBIND-035F is partitioned into seven dependency-ordered slices. CBIND-035F1 maps all 49 Viewport,
ClearOptions, GraphicsDeviceStatus, Unsupported3DGraphicsCallBehavior and SpriteEffects-operator
rows through a fixed 24-byte viewport POD with complete construction/property/transform/string
operations and fixed-width identities whose native ordinals are asserted at the adapter boundary.
The snapshot is now 3,215 implemented, 19 partial, 3,111 planned and 70 not applicable.
CBIND-035F2 then maps all 51 device lifetime, state, event, event-args, service and
device-exception rows through the borrowed device handle, owned event subscriptions with fixed
payload structures and a shared exception-firewall conversion. Its three GraphicsDevice friend
declarations become the first explicitly not-applicable rule rows and the four service-level
`IGraphicsDeviceService` events stay partial pending CBIND-037. The snapshot is now 3,259
implemented, 23 partial, 3,060 planned and 73 not applicable. CBIND-035F3 then maps all 8
TextureCollection and device texture-collection rows through stage-addressed slot reads, binds and
unbinds without exposing a native collection reference. The snapshot is now 3,267 implemented,
23 partial, 3,052 planned and 73 not applicable. CBIND-035F4 then maps all 21 clear, present,
reset, back-buffer-window and buffer-binding rows through versioned descriptors, a nullable adapter
index and caller-owned binding arrays. The snapshot is now 3,288 implemented, 23 partial, 3,031
planned and 73 not applicable. CBIND-035F5 then maps all 49 draw-submission and device-extension
rows through two descriptor-driven user-primitive calls, capability-gated buffered draws and the
complete CNAEXT helper set, closing every GraphicsDevice.hpp row. The snapshot is now 3,337
implemented, 23 partial, 2,982 planned and 73 not applicable. CBIND-035F6 then maps all 21
SpriteBatch text/mesh and OcclusionQuery rows through one text command covering every canonical
overload, a converted mesh descriptor and a capability-gated owned query handle. The snapshot is
now 3,358 implemented, 23 partial, 2,961 planned and 73 not applicable. CBIND-035F7 then maps all 118
`graphics-ext` rows through fixed identities, two settings-bag PODs and three owned post-process
effects whose routes report `NOT_SUPPORTED` when the opt-in extension layer is absent, so the
exported ABI never changes shape with the build option. The snapshot is now 3,476 implemented,
23 partial, 2,843 planned and 73 not applicable, and **no planned CBIND-035 inventory row
remains**; parent CBIND-035F is complete. CBIND-035G then adds the missing real-output evidence
through `Draw3DSmoke.c`: honest refusal on a backend without the 3D capability, and observable
pixel change through user, indexed, buffered and Model draw routes on the CPU-raster SOFTWARE
backend, with pixel readback treated as a capability separate from 3D. Parent CBIND-035 is
complete. CBIND-036A then closes the first CBIND-036 slice by mapping the whole 42-row `storage`
module: three strictly nested owned handle families, both canonical events, count/copy listings, a
C-native stream handle that never exposes `System::IO::Stream`, and the five fake-async Begin/End
pairs collapsed into single synchronous calls that still invoke the completion callback. Three
canonical failures gained boundary conversions -- `std::filesystem::filesystem_error` and
`System::IO::IOException` to `CNA_RESULT_IO`, `StorageDeviceNotConnectedException` to
`CNA_RESULT_INVALID_STATE` -- each proven in the adapter test. CBIND-036B1 then closes the
manager half of the content family (40 rows): resolved asset path and normalized cache key,
built-in loader registration, service-provider presence, graphics-device get/set, the manifest and
`.xnb` reader-usage snapshots as fixed PODs plus count/indexed copy, and typed Texture2D,
TextureCube and SoundEffect load routes. `System::IServiceProvider` stays a hard ABI boundary and
the `Load<T>`/`RegisterTypeReader<T>`/`RegisterCnjLoader<T>` templates are recorded as
inexpressible in C rather than given an invented untyped operation. The snapshot is now 3,545
implemented, 25 partial, 2,768 planned and 77 not applicable. CBIND-036B2 then closes the reader
half and with it parent CBIND-036B: an owned reader over an owned storage stream, an owned type
reader from the static registry or the known-unsupported placeholder factory, and the registry
itself. Type erasure is where the mapping stops, and that is recorded rather than papered over --
the two untyped read routes are `partial` because a type-erased C++ object has no C representation,
and every typed reader template, `LooseFileContentTypeReader<T>` and factory registration is
`not-applicable`. The snapshot is now 3,582 implemented, 28 partial, 2,704 planned and 101 not
applicable, with no planned `content` or `storage` row left. CBIND-036C then maps the 98 network
identity, value and packet rows: five enumerations at their canonical ordinals, the
quality-of-service value with both factories, an owned session-property list with an owned
enumerator, and owned packet buffers whose canonical color asymmetry is preserved and proved in
both directions. Two canonical gaps are closed on the C side rather than passed through -- the
unchecked `Insert`/`RemoveAt` indices and the enumerator's before-first dereference -- and the
join-failure conversion records the one payload a message cannot carry. The snapshot is now 3,665
implemented, 29 partial, 2,606 planned and 115 not applicable. CBIND-036D then maps the gamer,
machine and event-argument rows. Its boundary needed one correction: `LocalNetworkGamer` moved to
CBIND-036E because its receive and send paths dereference the owning session, so the slice is 47
rows rather than 65. The snapshot is now 3,711 implemented, 29 partial, 2,559 planned and 116 not
applicable. CBIND-036E is partitioned into five slices by what each part needs to exist, and
CBIND-036E1 closes the first: discovered sessions and their collection. The snapshot is now 3,728
implemented, 29 partial, 2,542 planned and 116 not applicable. CBIND-036E2 then maps the session
object itself. Two boundaries moved while implementing it: the three synchronous `Create` overloads
came here from CBIND-036E4, because none of the session's state is reachable without a session
object; and the minimum signed-in-gamer surface was borrowed from CBIND-037, because the canonical
session constructor selects its host from its local gamers and therefore cannot run with no gamer
signed in. The snapshot is now 3,792 implemented, 30 partial, 2,477 planned and 116 not applicable;
CBIND-036E3 then maps the ten session events as typed subscribe routes whose payload gamer handles
live only for the callback that receives them, and whose instance registrations hold a weak
reference so releasing one after its session is gone is a no-op. The snapshot is now 3,806
implemented, 30 partial, 2,463 planned and 116 not applicable. CBIND-036E4 then collapses every
canonical `Begin`/`End` pair into one synchronous C route that still invokes the completion
delegate, and maps discovery, join and the invited path alongside them. The snapshot is now 3,823
implemented, 30 partial, 2,446 planned and 116 not applicable, and `LocalNetworkGamer` is the only
`net` header with planned rows left. CBIND-036E5 closes it and with it parent CBIND-036: a local
gamer reuses the network-gamer handle and every route refuses a non-local one, all three receive and
all six send overloads are mapped, the sender comes back as a borrowed view, and three canonical
behaviors — the offset receive consuming its packet before rejecting the offset, the packet-reader
receive always reporting zero, and the declared-no-op voice and party-invite calls — are preserved
and asserted. The snapshot is now 3,841 implemented, 30 partial, 2,428 planned and 116 not
applicable, with no planned `storage`, `content` or `net` row left; CBIND-037 owns everything that
remains and is partitioned into seven module-sized slices. CBIND-037A closes the first of them, the
whole `core` module: one route per canonical logger static, the process-wide minimum level, the
compile-time platform, desktop operating system, renderer identity and renderer name, and both
backend classifications for any of the 46 public renderer identities. Two decisions are worth
recording. `CNA::CNAException` gained a central boundary conversion to `CNA_RESULT_INVALID_STATE`,
which is what makes the canonical non-desktop refusal of `getCurrentDesktopOS` observable in C
rather than collapsing into a generic internal failure. And the canonical log levels keep their
exact ordinals including the deliberate 100 for `EXPERIMENT`, so 6 is not an identity and is
refused. The snapshot is now 3,912 implemented, 30 partial, 2,356 planned and 117 not applicable,
with no planned `core` row left. CBIND-037B then splits the input module by device family and
opens it with CBIND-037B1: `GamePadType` at its canonical ordinals and the whole
`GamePadCapabilities` surface as one fixed 48-byte value with 35 directly readable and writable
flags, because every canonical property has both a getter and a setter. One boundary moved while
implementing it: `GamePad::GetCapabilities` came here from CBIND-037B3, because a capabilities
value with no producer cannot be tested against anything real. The snapshot is now 3,998
implemented, 30 partial, 2,270 planned and 117 not applicable. CBIND-037B2 then maps the five
gamepad value types onto the representations C already had rather than adding a second spelling of
the same numbers, and preserves three canonical behaviors worth naming: the thumbstick square clamp
and trigger clamp, the epsilon trigger comparison, and the directional pad's own hash weighting. It
also records one representational limit instead of hiding it — the C snapshot carries a single
button mask, so a supplied directional pad is merged into it, which is the relationship every state
CNA itself builds already has. The snapshot is now 4,063 implemented, 30 partial, 2,205 planned and
117 not applicable. CBIND-037B3 then maps the `GamePad` statics, keeping the canonical
availability-plus-answer shape of the sensor and touchpad queries instead of folding "no sensor"
into a failure, and borrowing the three CNA::Input identity enumerations three of those statics
return. The snapshot is now 4,108 implemented, 30 partial, 2,160 planned and 117 not applicable.
CBIND-037B4 splits again by device, and CBIND-037B4a closes the keyboard: the whole `KeyboardState`
value surface and every `Keyboard` static over the 256-slot bit field C already had, with the
canonical silent drop of an out-of-range key deliberately replaced by a refusal so nothing is lost
without the caller knowing. The snapshot is now 4,143 implemented, 30 partial, 2,125 planned and
117 not applicable. CBIND-037B4b then closes the mouse, including the static clicked event as an
owned registration that takes no game handle and a raise route that makes it observable without a
device. The snapshot is now 4,164 implemented, 30 partial, 2,104 planned and 117 not applicable.
CBIND-037B4c then closes the mouse cursor, whose stock singletons become borrowed views precisely
because their canonical disposal is a no-op, and records four honest `not-applicable` rows rather
than putting an `SDL_Cursor*` in the ABI. The snapshot is now 4,182 implemented, 30 partial, 2,082
planned and 121 not applicable. CBIND-037B4d closes text input and with it parent CBIND-037B4. This
is the first input family that is event-driven rather than sampled, so it is the first to carry
callbacks: all three canonical events become owned registrations sharing one release route, and the
`INTERNAL_On*` dispatchers become raise routes that make them observable without a keyboard. A
committed code unit is a `uint16_t` and an above-BMP code point arrives as two surrogate calls; the
composition and candidate payloads cross as `CNA_StringView`s borrowed only for the callback, so no
`std::string` or `std::vector` reaches the ABI. Writing its tests exposed something the earlier
input slices never had to face: a windowed backend publishes a real window into the canonical
static, so the family's behavior legitimately differs between trees. Rather than branching on the
renderer, the suite forces the unbound case to prove the null-guarded contract everywhere and then
restores what the backend really bound — which turned SDL_RENDERER into a genuine activate/
deactivate round trip instead of one more no-op assertion. The snapshot is now 4,209 implemented,
30 partial, 2,055 planned and 121 not applicable. CBIND-037B5 then closes touch and gestures by
extending what already existed rather than adding to the ABI: the whole `TouchCollection` mutation
surface operates in place on the fixed eight-slot snapshot the C API has carried since CBIND-025,
and only the gesture type and sample are genuinely new values. Its real work was archaeological.
Four canonical behaviors that a careless mapping would have quietly changed are now pinned by
tests: equality, the hash and the text are all blind to the pressure extension and the text carries
only the position; an empty gesture queue throws and so must refuse in C rather than hand back a
default sample; a raised touch event feeds gesture detection and never the snapshot, and is dropped
entirely until a display size is published; and `ResetForTests` clears the display metrics and the
window handle even though the canonical class comment says it leaves them alone. That last one is a
documentation-versus-implementation contradiction in the canonical header; the C contract follows
the implementation and states the discrepancy rather than repeating the comment. The snapshot is now
4,284 implemented, 30 partial, 1,975 planned and 126 not applicable. CBIND-037B6 then adds the
haptics family, the first input slice with no XNA counterpart at all and the first to produce an
owned handle rather than a value. Its central decision is that a closed device is an ordinary
object rather than an error: opening never fails for want of hardware, and every route on a device
with nothing behind it answers false, zero or -1 through its output. That is what makes a
force-feedback API testable on machines that have no force-feedback hardware — which is every
verification tree — and the closed path is asserted rather than skipped. Two representational
choices are recorded because a later reader would otherwise assume the obvious one: the custom
waveform travels beside the effect value rather than inside it, keeping that value a plain copyable
POD, and the device name is not part of the capability value, which is why the capability
comparison takes both names as arguments instead of silently comparing fewer fields than the
canonical operator does. The snapshot is now 4,407 implemented, 30 partial, 1,849 planned and 129
not applicable. CBIND-037B7a then completes the 54 raw-joystick rows and is the first input slice to
capture an owned **snapshot** handle rather than a value: four heterogeneous variable-length arrays
with no canonical maximum cannot honestly become a fixed POD, because any invented capacity
truncates real hardware and four separate queries would answer from four different instants. Two
things the plan had guessed wrong are corrected from the canonical source rather than assumed: the
POV hat is an ordinal identity, not a composable bit set, and the joystick facade returns plain
values, so only the snapshot and the two hot-plug registrations needed handle kinds (69 and 70). The
haptics closed-device contract carried over unchanged and is again the only path the verification
trees exercise. The snapshot is now 4,461 implemented, 30 partial, 1,795 planned and 129 not
applicable. CBIND-037B7b then completes the last 38 `input` rows — host sensors, device
enumeration, clipboard and power — and closes parents CBIND-037B7 and CBIND-037B, leaving the whole
`input` module mapped. Its three recorded decisions: the sensor reads leave the caller's reading
untouched when nothing answered, because that is what the canonical query does with its reference;
the device descriptor's identifier is 64-bit where the sensor and joystick ones are 32-bit, since a
touch-device identifier is natively that wide; and the clipboard setter reports that the request was
made rather than that the platform honored it, because the canonical setter returns nothing and this
ABI does not invent an outcome. The clipboard test therefore asserts a relationship and restores the
content it found. The snapshot is now 4,499 implemented, 30 partial, 1,757 planned and 129 not
applicable, with five modules fully mapped. CBIND-037C1 then opens the media module with its 25
identity, visualization and media-source rows, and adds the `cna_media` link edge the C API did not
have. Two decisions are recorded: the media-source identity keeps its canonical 0/4 gap rather than
being renumbered dense, so it has no maximum and consumers validate membership; and the canonical
source enumeration's `new`-ed pointers never cross the ABI — each route owns and destroys the list
it enumerated, which the sanitizer tree with leak detection proves. The snapshot is now 4,524
implemented, 30 partial, 1,732 planned and 129 not applicable. CBIND-037C2 then maps `Song` and
`SongCollection`. Its shape decision is reference-counted sharing: several handles may name one
song, which is what lets a collection retain the songs it was given where the canonical collection
only stores non-owning pointers. Three canonical behaviors are preserved rather than tidied — an
omitted name stays empty despite the constructor's own comment claiming otherwise, equality and the
hash come from the file path rather than handle identity, and `IsRated` is not "rating is nonzero" —
and three `Song` rows that return library-owned entities are re-partitioned into `CBIND-037C3`,
where those handles will exist. The snapshot is now 4,554 implemented, 30 partial, 1,695 planned and
136 not applicable. CBIND-037C3 then lands the whole music catalog — `MediaLibrary` plus albums,
artists, genres, playlists and their collections — after re-partitioning the library into this
slice, because none of the entity types is constructible from outside it. Everything except the
library is a borrowed view that keeps its library alive, so the library handle may be released
first. Two canonical facts were established by evidence rather than assumption: album equality pairs
the name with the artist because album names collide across artists, and `MediaLibrary(MediaSource*)`
**borrows** its argument rather than adopting it — a leak the sanitizer tree caught. The test builds
a deterministic fixture library by pointing SDL's user-folder lookup at a private directory, which
also keeps it from touching a real user's music. The snapshot is now 4,646 implemented, 30 partial,
1,578 planned and 161 not applicable. CBIND-037C4 then adds the picture surface, contributing two
shapes the family did not have: the picture-album **tree**, walkable because the root's absent
parent is an availability answer rather than a failure, and the ABI's first **point in time** — a
picture's date as 100-nanosecond ticks from the Unix epoch, reusing the existing tick rather than
inventing a second time unit. The stream-taking save overload accepts a storage stream handle, the
only byte source this ABI owns. The snapshot is now 4,694 implemented, 30 partial, 1,518 planned and
173 not applicable. CBIND-037C6 then maps playback: the static `MediaPlayer` as game-scoped free
routes and the queue as a view of one process-lifetime object. Its two deviations are both forced by
ownership — a queue entry crosses as an owned copy because the canonical queue destroys its entries
on every clear, and appending copies because the canonical `Add` adopts the pointer it is given —
and both copies compare equal to the original, which is exactly what the canonical player does when
it enqueues a song. The playback transitions are asserted as a relationship, because whether a play
call really starts playing depends on the platform's decoder rather than on the C API. The snapshot
is now 4,734 implemented, 30 partial, 1,474 planned and 177 not applicable. CBIND-037C7 then closes
the media module with video. Its frame texture is solved by lifetime rather than by copying — the C
layer invalidates the borrowed handle on the next call to that player, so a stale frame fails
deterministically — and three canonical behaviors are reported rather than corrected, two of them
discovered by running the code: an undecodable file leaves the player stopped with its video
cleared, and the URI factory does not parse URIs at all. The snapshot is now 4,775 implemented, 30
partial, 1,432 planned and 178 not applicable, with six modules fully mapped. CBIND-037D1 then
opens the devices module with the sensor reading values, settling the ABI's second point-in-time
form — ticks from 0001-01-01 plus a UTC offset, because that is the canonical runtime type's base —
and preserving three canonical quirks rather than tidying them: each reading constructor keeps its
own argument order, equality pairs values with the timestamp, and the text conversions carry only
part of each reading. The snapshot is now 4,844 implemented, 30 partial, 1,362 planned and 179 not
applicable. CBIND-037D2a then maps the two motion sensors that produce those readings, deciding
that a class-template base is repeated per sensor rather than modeled, that an event delivering
nothing but a reading hands over the reading, and that the canonical **test-support** surface is
mapped deliberately — it is the only way a machine with no motion sensors reaches the supported path
and the real dispatch chain. Three canonical behaviors are reported rather than smoothed: an
unsupported sensor refuses to answer its current value, a second disposal is refused where every
other disposable in this ABI is idempotent, and the disposed state has no query route because the
canonical flag is protected. The snapshot is now 4,904 implemented, 30 partial, 1,282 planned and
199 not applicable.

## Handoff for the next context / Claude Code (2026-08-15)

Read *Current status* above first: it carries the snapshot, what is closed, and the ordered list of
what remains. This section carries only what a fresh context cannot infer from the plan.

### Where things stand

- Branch: `feature/binding`. `CBIND-037D2a` is the last task completed; the working tree is clean
  and every slice below is committed one-task-one-commit. **The whole `input` module is closed** —
  834 implemented, 27 not applicable, no partial and no planned row — as are `storage`, `content`,
  `net` and `core`.
- **Next task:** `CBIND-037D2b`, the rest of the sensors — **46 rows**: `Compass`, `Motion`, the
  reading event-argument types, `CalibrationEventArgs`, and `Accelerometer::ReadingChanged`, the
  obsolete legacy event `D2a` deliberately left planned so it lands with the type it delivers.
  `D1` and `D2a` are done, so `sensors.h`, `CnaCApiSensors.cpp`, `SensorValuesSmoke.c` and
  `SensorDeviceSmoke.c` exist, `cna_c_api` links `cna_devices`, the reading values are mapped, and
  the whole owned-sensor-handle shape — support probe, state, start/stop, current value, data
  validity, update interval, reading callback, error id, test-support injection — is settled and
  should be repeated, not redesigned.

  Four things are already known and should not be rediscovered: `SensorState` records that
  `NoData` and `NoPermissions` are produced by **no** sensor class today, so those two identities
  will stay untested by behavior; `SensorReadingEventArgs<T>` is a **template**, and only its four
  concrete instantiations matter — `D2a` already flattened its event to the reading it carries, and
  `D2b` should decide once whether the wrapper types earn C values at all; `SensorFailedException`'s
  **error id** already reaches C through `cna_sensors_get_last_error_id_ext`, recorded by the
  barrier exactly as the network join error is, so `D2b` only has to use it; and `Compass` is an
  honest `NotSupported` stub on every platform but Android, so no verification tree will ever reach
  its supported path or fire its calibration event.
- **The `CNA_DEVICES` environment decision is done, not pending.** The owner directed (2026-08-15)
  that the `#ifdef CNA_DEVICES` half of `devices-ext` be genuinely exercised rather than only ever
  tested compiled-out. `cmake-build-binding-sdlrenderer` and `cmake-build-binding-asan` have been
  **reconfigured with `-DCNA_DEVICES=ON`** and rebuilt; `headless` and `software` stay OFF, so both
  states are covered, mirroring the existing `CNA_CNAEXT` split. No fifth build tree was added, and
  all four trees are green in that configuration. The routes themselves must stay exported in both
  states, following the `CnaCApiGraphicsExt.cpp` precedent of an `#ifndef` fallback that reports the
  feature as unavailable, so the ABI's symbol set never depends on a build option.
- Do not reopen a closed slice without a concrete demonstrated defect.
- The four verification trees and the shared ccache are set up and warm; nothing needs configuring
  before the next slice. See *Environment and disk hygiene* below for what was cleaned up on
  2026-08-15 and what must not be undone.
- **Check for a second Claude session before writing anything.** See the trap entry below; this
  campaign lost work to it twice in one afternoon.

### Code map — what one slice touches

The C API lives entirely in `modules/c-api/`. A slice almost always edits exactly these, in this
order:

| File | Role |
|---|---|
| `include/CNA/C/<family>.h` | the public surface. One header per family — 55 today (`sensors.h`, `video.h`, `media_player.h`, `media_library.h`, `media.h`, `input_devices.h`, `input_joystick.h`, `input_gamepad.h`, `input_keyboard.h`, `input_mouse.h`, `input_cursor.h`, `input_text.h`, `input_touch.h`, `input_haptics.h`, `net_sessions.h`, `storage.h`, `core_ext.h`, …). Add a new one when the family is genuinely new; extend an existing one when it is not. |
| `include/CNA/C/cna.h` | the umbrella. **Every new header must be added here** or a strict-C consumer never sees it. |
| `src/CnaCApi<Family>.cpp` | the adapter — 45 files today. Routes go in `extern "C"` scope; helpers in an anonymous namespace above them. |
| `src/CnaCApiDetail.hpp` | shared substrate: the `ObjectKind` handle-kind enum (**next free number is 91**), the `HandleRegistry`, `CallWithExceptionBarrier` and its 18 exception arms, `CopyStringView`, `Fail`. A new handle kind or a new canonical exception conversion lands here. |
| `src/CnaCApi<Family>Detail.hpp` | cross-file borrow helpers, when one family's adapter must reach another's resource (`CnaCApiGraphicsDetail.hpp` exposes `GetOwnedTexture2D`, `CnaCApiNetDetail.hpp` exposes `BorrowPacketReader`, …). |
| `CMakeLists.txt` | the `cna_c_api` source list, and the per-test executable + `add_test` block (59 tests today). |
| `tests/pure_c/<Family>Smoke.c` | the strict-C17 behavior test. 55 files; prefer extending the family's existing one over adding a target — but a family with its own adapter file has earned its own test target, as haptics did. |
| `tests/pure_c/AbiHeaderC.c` and `tests/cpp/AbiHeaderCpp.cpp` | freeze every new identity value and every new struct size/alignment/offset. Both must compile — the surface has to be valid C17 *and* C++23. |
| `tests/cpp/BoundaryDetailTest.cpp` | only when a slice adds an exception-firewall arm; returns a distinct code per case. |
| `tools/c-api/coverage_mappings.json` | the rules that close inventory rows. |
| `docs/c-api/<FAMILY>.md` + `FEATURE_MATRIX.md` + `README.md` | the consumer-facing contract. |

The design contracts that already exist do not need restating in a new page — point at
`docs/c-api/HANDLES.md`, `OWNERSHIP.md`, `STRINGS_AND_BUFFERS.md`, `ERRORS.md` and
`CALLBACKS_AND_THREADING.md`.

### Conventions already settled (do not reinvent)

Every closed slice follows these. A new slice that invents a different shape makes the ABI
inconsistent, which is worse than the shape being slightly suboptimal.

- **Identities** are `typedef uint32_t CNA_Xxx;` plus `#define CNA_XXX_* UINT32_C(n)` at the
  canonical ordinals — never renumbered into a dense range (`CNA_LOG_LEVEL_EXPERIMENT` is 100).
  Flag sets get a `CNA_XXX_ALL` mask and every route validates against it.
- **Values** are fixed PODs. Anything that may grow carries `struct_size` + `struct_version` and
  an `_init` route; a pure math pair (`CNA_GamePadThumbSticks`) does not.
- **Strings** are the count/copy pair `cna_x_get_y_size` + `cna_x_copy_y`: **no terminator**,
  `out_bytes` always required and always written, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial
  write** when the capacity is short. Input strings are a borrowed `CNA_StringView`, copied before
  use.
- **Handles** are opaque, generation-checked and thread-affine. Owned vs borrowed is a deliberate
  choice per type: a borrowed view keeps its parent alive and blocks the parent's release.
- **`_ext` suffix** marks a route with no XNA 4.0 counterpart — either the canonical member is
  `CNAEXT`, or the route exists only because C needs it. A whole header of CNA-namespace surface
  (`core_ext.h`) does not repeat the suffix on every route.
- **Every fallible route** returns `CNA_Result` and runs inside `CallWithExceptionBarrier`.
- **Availability is separate from the answer.** A canonical query that returns `bool` and fills an
  output reference becomes a route with an `out_available` flag — "no sensor" is an ordinary answer,
  never a failure.
- **An identity is not a capability claim.** Never branch a test or a route on which renderer is
  compiled in; probe the behavior.
- **Preserve canonical quirks, record deliberate deviations.** Both have precedent in the closed
  slices; what is not acceptable is silently smoothing one over.

### The loop for one slice

```bash
cd /rv/data/development/github.com/openeggbert/cnabinding
export CCACHE_DIR=/media/robertvokac/claude/tmp/cna/ccache
B=/media/robertvokac/claude/tmp/cna/cmake-build-binding-headless

# 1. what does this slice own?  (planned rows, by header)
python3 - <<'EOF'
import re
hdr=None
for line in open('docs/c-api/COVERAGE.md',encoding='utf-8'):
    m=re.match(r'#### `(modules/[^`]+)`', line)
    if m: hdr=m.group(1); continue
    if line.startswith('| `CPP-') and '⬜' in line and '<Header>.hpp' in (hdr or ''):
        print(line.split('|')[4].strip())
EOF

# 2. implement, then build just the library
nice -n 10 cmake --build $B --target cna_c_api -j3

# 3. probe a new struct layout before freezing it
gcc -std=c17 -I modules/c-api/include /path/to/probe.c -o /tmp/probe && /tmp/probe

# 4. coverage: regenerate, then CHECK THE DELTA equals the slice's row count
python3 tools/c-api/generate_coverage_inventory.py --write
python3 tools/c-api/generate_coverage_inventory.py --check

# 5. all four trees
for T in headless sdlrenderer software asan; do
  D=/media/robertvokac/claude/tmp/cna/cmake-build-binding-$T
  nice -n 10 make -C $D/modules/c-api -j3 || break
  (cd $D && SDL_VIDEODRIVER=dummy ASAN_OPTIONS=detect_leaks=1 \
      ctest --test-dir modules/c-api --output-on-failure -j3 | tail -3)
done
```

A slice is not finished until step 4's delta matches, all four trees are green, and
`plan_binding.md` / `AUDIT.md` / `NEXT.md` / the `docs/c-api/` pages say what changed.

### Verification (do all four before committing a slice)

All four trees live under `/media/robertvokac/claude/tmp/cna/` (off the repo, on the scratch
partition, sharing the project-wide `CCACHE_DIR=/media/robertvokac/claude/tmp/cna/ccache`).
**Do not give the binding trees a cache of their own.** They were pointed at a separate
`tmp/ccache` until 2026-08-15; it reached a 0.69% hit rate over 6,932 compilations because it
started cold and never saw the CNA and sharp-runtime objects the shared cache already holds.
It was deleted. Build only `modules/c-api` in
each — `make -C <tree>/modules/c-api -j3` — never the default `all` target, which pulls in
unrelated modules and examples. Then `ctest --test-dir modules/c-api`. Cap parallelism at `-j3`.

| Tree | Configuration | Why it exists |
|---|---|---|
| `cmake-build-binding-headless` | `HEADLESS`, `CNA_CNAEXT=OFF` | deterministic state; the no-extension-layer half |
| `cmake-build-binding-sdlrenderer` | `SDL_RENDERER`, `CNA_CNAEXT=ON` | the extension-layer half; needs `SDL_VIDEODRIVER=dummy` |
| `cmake-build-binding-software` | `SOFTWARE` | the only tree that can supply real 3D pixel evidence |
| `cmake-build-binding-asan` | `SOFTWARE`, `CNA_CNAEXT=ON`, `CNA_SANITIZE=address,undefined` | verification only |

All four run the same 59 C API tests green. The sanitizer tree runs with
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1` — stricter than the
`detect_leaks=0` the CBIND-035B–E slices used; **do not weaken it back**. Every tree needs
`-DCNA_BUILD_C_API=ON`, which defaults to OFF: a freshly configured tree silently has no
`modules/c-api` build directory at all without it.

**Never branch a test on a renderer identity.** Probe the capability or the actual result, so a new
backend needs no test edits. `CApi_TextureSmoke`, `CApi_TextureVolumeSmoke` and `CApi_LifecycleSmoke`
were rewritten once for exactly this reason.

### Traps this campaign actually hit

- **Coverage rules on free operators need a `signature_regex`.** `^CNA::Input::operator\|$` with no
  signature also swallowed `HapticFeatureEXT`'s five operators and claimed haptics coverage that
  does not exist. Caught only by comparing the implemented delta against the slice's row count —
  **always do that comparison** after regenerating.
- **`sharp-runtime` is a sibling checkout other sessions edit and commit to mid-build**
  (`/rv/data/development/github.com/openeggbert/sharp-runtime`). A build failure inside it is very
  likely someone else's work in progress, not a regression here: run `git status` and `git log` in
  *that* tree before diagnosing, and never modify it from this task.
- **A windowed backend publishes a real window into the input statics.** `GraphicsDevice` calls
  `TextInputEXT::setWindowHandleProperty` and `Mouse::setWindowHandleProperty` when it attaches or
  creates a window, so under `SDL_RENDERER` the handle is *not* zero, while HEADLESS and SOFTWARE
  leave it at zero. A test that asserts the initial value is zero passes in three trees and fails in
  the fourth — which is the identity-versus-behavior rule biting from the other side, since the
  assumption is about the environment rather than the renderer name. The fix that also produced
  *better* evidence: force the unbound case to prove the null-guarded contract everywhere, then
  restore whatever the backend really bound and assert only the *relationship* between the answers
  (activation that took effect must be undone by stopping it). On `SDL_RENDERER` that is a real
  activate/deactivate round trip. `TouchPanel` has the same window-handle property and `CBIND-037B5`
  did meet it again — any route that resets this state must put it back, because it is process-wide
  state the suite does not own. Expect the remaining `CNA::Input` families to have their own.
- **Out-parameter clobbering.** Routes set `*out = CNA_INVALID_HANDLE` before validating, so
  reusing one variable for an expected-failure call destroys a live handle. Hit three times; use a
  separate scratch variable for the failure case. **Versioned output structures have the same
  problem in a nastier form:** `CBIND-037B5` set `struct_version = 2` on a live `CNA_GestureSample`
  to test the invalid-structure refusal and every *later* read of that variable then refused, which
  looks like a broken route rather than a broken test. Copy to a scratch value.
- **A canonical Doxygen comment can contradict its own implementation.** `TouchPanel::ResetForTests`
  documents that the display size and orientation are left untouched; it clears them, plus the
  window handle, deliberately — a leaked display size silently corrupts another test's scaled touch
  coordinates. Read the `.cpp`, not just the header, before writing a C contract sentence, and when
  they disagree follow the behavior and say so in the C header. Assume other stale comments exist.
- **Two sources feed one snapshot.** `TouchPanel::INTERNAL_onTouchEvent` feeds gesture detection and
  the event-driven touch map; `TouchPanel::SetFinger` feeds the slot array that `GetState` actually
  reports. A test that raises an event and then asserts the snapshot changed will fail while looking
  like a mapping bug. The raise path is also a **silent no-op** until a display size is published.
  Expect more of these splits in the remaining `CNA::Input` families.
- **Do not pipe a build into `grep … | head`.** SIGPIPE kills the build and leaves a target
  unlinked, which then shows up as a mysterious "Not Run" in ctest. Redirect to a log and grep the
  log.
- **A second Claude session on this working tree will silently destroy your edits.** It happened
  twice on 2026-08-15, and both times the first symptom was `git status` listing files the session
  had not touched, or a file changing mtime mid-read. The damage: a duplicated
  `validate_text_input_family` block appended to `InputSnapshotsSmoke.c`, and a read-modify-write
  that could have dropped the other session's concurrent write. It also produced a **false test
  failure** — a `sdlrenderer` red that looked like a mapping bug but was another session's
  mid-flight state. Detect it before writing: `ListAgents`, or
  `ps -eo pid,args | grep "[c]laude" | grep "resume .*cnabinding"`. Background sessions run with
  `--permission-mode auto`, survive their terminal being closed, and can respawn after being killed.
  If one is active, agree on who owns the slice *before* writing, and never build the same tree
  concurrently — a spurious `ranlib` failure from exactly that is already on record.
- Read the canonical `.cpp`, not just the header. Several slices turned on behavior only the
  implementation reveals: a square clamp, an epsilon comparison, a silent drop, a no-op disposal,
  a hash that ignores half the fields.

### Environment and disk hygiene

Settled on 2026-08-15 after an audit; a future context should keep it this way rather than
rediscover it.

- **One shared ccache, not one per campaign.** `CCACHE_DIR=/media/robertvokac/claude/tmp/cna/ccache`
  (20 GB ceiling, ~31% hit rate across 21 build configurations). The binding trees briefly had their
  own `tmp/ccache`; it reached a **0.69% hit rate over 6,932 compilations** because it started cold
  and never saw the CNA and sharp-runtime objects the shared cache already holds. It was deleted.
  Do not give these trees a private cache again.
- **Do not shrink the shared cache.** It sits at 96.5% of its ceiling and still misses 69% of the
  time, which means it is undersized for this workload, not oversized. Shrinking it causes more
  compilation, which means more SSD writes — the opposite of what the build rules are protecting.
- **Never build `all` in a binding tree.** Someone did, once, in
  `cmake-build-binding-headless`: it left **56 stray executables and their object trees, 2.6 GB**,
  none of which the C API loop ever links against. They were deleted; the tree went 3.7 GB → 1.1 GB
  with zero recompilation afterwards. Build `make -C <tree>/modules/c-api -j3` and nothing else.
  To check a tree for the same rot:
  `find <tree> -maxdepth 1 \( -name 'cna_test_*' -o -name 'cna_demo_*' -o -name 'CnaTests' \)`.
- **The four binding trees are the only build directories this campaign owns.** Everything else
  under `/media/robertvokac/claude/tmp/cna/` (`cmake-build-multi`, `gltf-*`, `fna3d*`,
  `develop-opengles`, `next-*`, `software`, `sdlgpu`, …) belongs to other checkouts and other
  sessions. Do not delete or build in them.
- Session scratchpads and per-run build logs are disposable; write throwaway probes there, never a
  build tree.

### Standing constraints

- `analysis_binding.md` and `analysis_binding_sharp_runtime.md` are **strictly read-only**.
- Only the C binding is in scope. Do not plan or implement C#, .NET, JavaScript, Rust, Python,
  Java, Zig, Go, Swift or any other language binding.
- One task, one commit. Stage explicit file names; never `git add -A`. Do not push unless asked.
- Build targets are `cna_c_api` plus the strict-C and C/C++ ABI targets; there is no `CNA` target
  in the configured build trees.
