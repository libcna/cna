# Native C API and language-consumer boundary

CNA implements its framework/runtime once in C++. Its native C API exposes that implementation
through fixed-width values, typed opaque handles, caller buffers, result codes and C callbacks.
The detailed contract is [c-api/README.md](c-api/README.md); this page explains the architectural
boundary and how to assess a consumer.

## Approved scope and current status

The approved planning boundary in [AGENTS.md](../AGENTS.md#language-bindings) is CNA's native
C API/C ABI. Language-specific binding plans and implementations require explicit project-owner
authorization. Documentation about an existing external consumer is not authorization to start
another binding or modify a consumer repository.

The implementation lives in `modules/c-api`, with public headers under `CNA/C/`. It remains an
experimental `0.x` ABI. Its version, change ledger and compatibility rules are maintained in
[c-api/ABI_VERSIONING.md](c-api/ABI_VERSIONING.md), with constants in
[abi.h](../modules/c-api/include/CNA/C/abi.h).

[COVERAGE.md](c-api/COVERAGE.md) is the generated native public-surface inventory, not a promise
that every C++ member has a C route or that every exported route works on every platform.
[RELEASE_GATE.md](c-api/RELEASE_GATE.md) currently reports the experimental release **not ready**;
its remaining public-surface accounting criterion must be resolved before claiming that gate is
met. Passing the gate checker means the recorded verdict is consistent, not that its verdict is
Ready.

External language consumers own their ABI admission, public API, packaging and platform evidence.
The [handoff](../handoff.md) contains historical consumer pins and measured findings. Consult each
consumer's own checkout/release evidence for its current status; CNA's native documentation cannot
certify an external repository's latest compatibility.

## One native implementation, one public C boundary

A C consumer calls the C adapter, which calls CNA's C++ subsystems, renderers and platform
abstraction. Sharp Runtime remains below that boundary as an implementation dependency. A
language-specific consumer, when separately authorized, adds its own presentation and interop
layer above the same C ABI.

C++ standard-library objects, templates, inheritance, virtual dispatch, exceptions and compiler
object layouts stay inside CNA. Domain routes such as `cna_texture2d_*`, `cna_sprite_batch_*`
and `cna_content_manager_*` expose framework behavior without exporting a general-purpose
Sharp Runtime object system. Native fixes and C adapter changes must be reviewed together when
they affect that public contract.

The native XNA layer keeps the XNA/FNA API and behavior under the project rules. Measured XNA
disagreements take precedence and are recorded with evidence in
[xna-fna-behavior-decisions.md](xna-fna-behavior-decisions.md). A C consumer still uses explicit
C-native representations rather than C++ overload resolution or templates.

## Boundary rules a consumer must preserve

| Area | Current contract |
|---|---|
| Values and identities | Fixed-width integers, POD values, `CNA_Bool` as canonical 0/1, and fixed-width identity typedefs. Small native types need not be handles. |
| Object identity | `CNA_Handle` is a process-local 64-bit token. Copying it does not retain ownership; two different tokens can alias one native object. |
| Lifetime | Owned handles, owned views, protected existing tokens and callback loans have different release rights. Device resources follow their own parent, not a universal game parent. |
| Errors | Fallible routes return `CNA_Result`; per-thread diagnostics and documented supplemental queries carry details. Native exceptions are translated at the boundary. |
| Text and arrays | Explicit UTF-8 byte views in; caller-owned size/copy output or a documented shorter borrow. Units and failure output timing are route-specific. |
| Structs | Versioned descriptors use `struct_size` and `struct_version`; plain POD values do not all have that prefix. Array stride is defined by its route. |
| Threads/callbacks | Handle creation-thread affinity is the default. Pure-value helpers, two sound-play routes and opt-in foreign-thread service have explicit exceptions. |
| Features | Query the active renderer/profile/platform. Enum presence, successful compilation and handle creation alone do not prove every operation's support. |

The authoritative details are [HANDLES.md](c-api/HANDLES.md), [OWNERSHIP.md](c-api/OWNERSHIP.md),
[ERRORS.md](c-api/ERRORS.md), [STRINGS_AND_BUFFERS.md](c-api/STRINGS_AND_BUFFERS.md) and
[CALLBACKS_AND_THREADING.md](c-api/CALLBACKS_AND_THREADING.md).
Capability/default fallback behavior is documented per operation in
[RENDERERS_AND_CAPABILITIES.md](c-api/RENDERERS_AND_CAPABILITIES.md).

The registry is process-wide within the loaded library. One C-owned game may be active, while
multiple independent devices and game-independent objects can coexist. There is no public
runtime-handle create/destroy API or generic retain/release/kind-query operation.

A non-null C pointer still has to reference valid storage of the claimed capacity. Result codes,
generation checks and exception translation cannot validate freed caller memory or recover from
every native/driver process failure. Current reproduced implementation defects remain visible in
[handoff.md §5](../handoff.md#5-known-bugs-and-limitations).

## Native types, streams and completion

Sharp Runtime supplies native `System.*` concepts internally. Consumers need no Sharp Runtime
headers, string/collection layouts, allocator rules or C++ exception ABI.
[SHARP_RUNTIME_BOUNDARY.md](c-api/SHARP_RUNTIME_BOUNDARY.md) lists the actual C mappings.

Streams are exposed where supported as typed storage-stream handles; other routes take encoded
bytes or paths. There is no general stream callback table to infer from the native Stream type,
and no general future/poll/cancel operation-handle API. Storage and networking completion routes
wait synchronously at the C boundary. Guide keyboard/message-box operations use their documented
pending/completion contract in [GAMER_SERVICES.md](c-api/GAMER_SERVICES.md).

Math and snapshot values have explicit C POD forms and native helper routes. Bulk operations move
whole sprite arrays, textures, vertices, indices or samples, and input queries inspect captured
snapshots. The native API defines these representations and behaviors without prescribing a
separate language's wrapper implementation.

## Game lifecycle and cleanup

`CNA_GameCallbacks` and `CNA_GameFrameHooks` adapt application callbacks to the native window,
event pump, timing and frame lifecycle. The tables are copied, but callback functions and contexts
must remain valid for their documented lifetime. Lifecycle failures use the callback error channel;
event callbacks follow their individual signatures.

A lifecycle callback can perform callback-safe operations and request exit. Recursive game driving
and game destruction are refused while callback scope is active. A borrowed game-device token
expires at the end of its documented callback/pre-run window; resources created through it can
survive that window with their own release obligations.

Release tracked children on the appropriate owner thread before destroying their parent.
Registration lifetime is family-specific. Explicit generic resource disposal may leave a valid
handle pointing to disposed native state, and a game destroy can invalidate its handle while
returning a callback failure. Use the family contracts instead of treating all failed calls as
ownership-preserving.

## Build, version checks and evidence

[CONSUMING.md](c-api/CONSUMING.md) documents the installed `CNA::CApi` CMake target, native
dependency packaging and optional static target. The package includes CNA-built runtime libraries;
external system dependencies and build-mode requirements still apply. The browser artifact has a
separate path in [WASM_ARTIFACT.md](c-api/WASM_ARTIFACT.md).

Check `cna_get_abi_version()` at startup. During `0.x`, admit only reviewed minor versions;
a same-major CMake package match alone is insufficient. ABI changes follow
[ABI_VERSIONING.md](c-api/ABI_VERSIONING.md), including its struct-extension and incompatible-change
rules.

The standalone [hello_cna.c](../modules/c-api/examples/c/hello_cna.c) example is the native C
starting point: it checks the ABI, creates callbacks, borrows a device, queries capabilities,
creates/uploads/draws a texture, reads diagnostics and shuts down. It creates its pixels directly;
it does not demonstrate content loading or input capture.

Verification has distinct layers: native behavior tests, C adapter/ABI/lifetime tests, installed
consumer execution, header compilation and platform/renderer qualification.
[COMPATIBILITY.md](c-api/COMPATIBILITY.md) is specifically a per-header compiler/language matrix,
with C99 as the public-header floor; C17/C++23 targets supply additional layout checks.
Header compilation is not runtime evidence, and generated route coverage is not a behavior test.

Use [LIMITATIONS.md](c-api/LIMITATIONS.md), the generated coverage/release reports and the relevant
subsystem pages when deciding which native behaviors an application can rely on. External
consumers need their own import/layout/version, lifetime, callback, packaging and behavior evidence
for the exact CNA build they use.
