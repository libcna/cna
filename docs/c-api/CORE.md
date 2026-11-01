# CNA C API core contract

## Scope

The core C surface is split into three layers:

- `CNA/C/abi.h` defines export visibility, ABI versioning, result codes, `CNA_Bool` and the opaque
  `CNA_Handle` representation.
- `CNA/C/core.h` adds the per-thread error record, counted strings and the small value structures
  shared by many subsystem headers.
- `CNA/C/core_ext.h` maps CNA's non-XNA core module: logging, target-platform facts, renderer
  metadata and process-wide renderer selection, plus the application title.

These routes need no game, graphics device or runtime initialization and create no object handles.
Some of them do mutate process-wide state, however: the logger threshold and sink, renderer
selection, and assembly title are shared by the whole process.

## ABI primitives

`CNA_ABI_VERSION` encodes the public major, minor and patch values into one `uint32_t`, and
`cna_get_abi_version` reports the library's value at runtime. Compatibility rules and the current
experimental version are documented in [`ABI_VERSIONING.md`](ABI_VERSIONING.md).

Every fallible route returns a fixed-width `CNA_Result`. The public set currently runs from success
through buffer-too-small and is paired with a stable `CNA_ErrorCategory` where a failure records a
diagnostic. No C++ or Sharp Runtime exception crosses the ABI. The complete result mapping belongs
in [`ERRORS.md`](ERRORS.md).

`CNA_Bool` is one byte, and only `CNA_FALSE` and `CNA_TRUE` are valid inputs. Values such as `2`
are rejected rather than interpreted as truthy. `CNA_Handle` is a 64-bit opaque,
generation-checked identity; `CNA_INVALID_HANDLE` is zero. Handle ownership and parent ordering are
documented in [`HANDLES.md`](HANDLES.md), not inferred from the integer representation.

## Counted strings and shared values

`CNA_StringView` is a borrowed pointer plus byte count. It is not NUL-terminated and is valid only
for the call receiving it. A null pointer is valid only with a zero count. The view itself is only a
carrier: each route documents whether an empty string or embedded NUL is meaningful, refused or
copied literally. Text-taking core routes validate UTF-8 and copy any value they retain.

The shared structures in `core.h` are plain C values:

- `CNA_Color` is unpacked RGBA with four 8-bit channels; it is not the packed integer layout of the
  native XNA `Color` object;
- `CNA_Vector2` and `CNA_Vector3` are consecutive `float` components; and
- `CNA_Rectangle` is four signed 32-bit values: position followed by size.

Larger math and graphics structures live in their owning headers. Their ABI layouts are checked by
the C and C++ ABI assertion suites.

## Per-thread error information

On failure, CNA records the result, category and UTF-8 message for the calling thread.
`cna_error_get_last_info` reads the fixed fields through versioned `CNA_ErrorInfo`; the message uses
the standard size/copy pair. A capacity refusal reports the required count and writes nothing.

The error-query routes deliberately do not overwrite the diagnostic they inspect, even when the
query itself receives a bad argument. A successful ordinary call also leaves the previous failure
available. The record changes only when a later failed operation on the same thread replaces it;
another thread has an independent record.

Two extension pairs additionally expose the fully qualified canonical exception type and an
argument exception's parameter name. They are empty when the C adapter itself rejected an argument
or handle without catching a canonical exception. This lets a higher-level binding re-raise
`System.ArgumentOutOfRangeException` or
`Microsoft.Xna.Framework.Content.ContentLoadException` instead of guessing from the coarser result
code. C programs should still branch on `CNA_Result`, never on diagnostic text.

The exception firewall maps known failures to their documented results. In this module, for
example, `CNA::CNAException` becomes `CNA_RESULT_INVALID_STATE` with state category, while an
unexpected native exception becomes `CNA_RESULT_INTERNAL`. See [`ERRORS.md`](ERRORS.md) for the
full mapping and callback-failure rules.

## Logging

The logger is a static, process-wide utility, so there is no logger handle. The general
`cna_logger_log` route takes level, message, category and condition explicitly. The seven named
severity routes take message and category; their conditional siblings take message and condition
because the native conditional overloads always use the `APPLICATION` category.

All seven levels and nine categories are fixed public identities. Level values are deliberately
not contiguous: `CNA_LOG_LEVEL_EXPERIMENT` is `100`, and `6` is invalid. The minimum-level setter
changes native process-wide state seen by every thread and CNA subsystem. A test or library that
changes it must restore the previous value.

Messages are copied and reject invalid UTF-8 or embedded NUL. A false condition succeeds without
emitting. The default sink writes a formatted `[LEVEL][CATEGORY] message` line to stderr, never
stdout, because terminal rendering may own stdout.

`cna_logger_set_sink_ext` replaces that destination with one C callback; a null callback or
`cna_logger_reset_sink_ext` restores stderr. Delivery is synchronous on the thread that emitted the
line and serialized by the native logger. The message bytes are callback-scoped. The callback must
return normally and must not re-enter CNA. Its context remains caller-owned and must live until a
replacement/reset call returns; after that return the old callback is no longer in flight.

## Target platform

`cna_platform_get_current` reports the build target: desktop, Android, iOS or web. The Apple and
mobile helpers and the current-platform name are compile-time facts as well; they do not probe the
host at runtime. Apple is true for macOS and iOS, while mobile is true for Android and iOS.

`cna_desktop_os_get_current` refines a desktop target to Windows, Linux, macOS or Other. On Android,
iOS or web the canonical query throws instead of inventing a desktop fallback, and the C firewall
returns `CNA_RESULT_INVALID_STATE`. Code that is portable across target families checks the
platform first.

## Renderer identities, categories and maturity

The C ABI currently publishes 10 live renderer identities. Their values are stable, sparse and
never reused: `CNA_GRAPHICS_RENDERER_UNKNOWN` is report-only, retired renderers leave permanent
numeric gaps, and `CNA_GRAPHICS_RENDERER_MAXIMUM` is the highest live value rather than a count.
The authoritative identity table is [`../renderer-registry.md`](../renderer-registry.md).

Category describes implementation technology; maturity describes recommendation confidence. The
classification routes accept any live public identity, even one not compiled into this binary, and
reject unknown or retired values. Their `current` variants describe the build's compile-time
default. Category and maturity names use count/copy routes so no native `string_view` crosses the
ABI.

`cna_graphics_renderer_get_current_type` and its name pair also describe only that compile-time
default. In a multi-renderer build they do not change when runtime selection chooses another
renderer. Identity, category and maturity are metadata, not feature guarantees; query the active
device's capability profile for operation-level decisions.

## Runtime renderer selection

The process-wide selection family distinguishes four questions:

| Question | C route |
|---|---|
| What is the build default? | `cna_graphics_renderer_get_current_type` |
| What will CNA try first? | `cna_graphics_renderer_get_selected_ext` |
| Which identities were compiled into this binary? | available count/copy and `get_is_available` |
| Which renderer successfully created the device? | `cna_graphics_renderer_get_active_ext` |

An application may set the preferred identity or its case-insensitive canonical name before the
first graphics device succeeds. Explicit selection has precedence over the
`CNA_GRAPHICS_RENDERER` environment value, which has precedence over the build default. Once a
renderer is active, selection is latched and mutating routes return `CNA_RESULT_INVALID_STATE`.
Asking for the active renderer before the latch is also a state error rather than a guess.

Fallback is opt-in. A caller may supply an ordered chain or enable the native automatic policy.
Every rejected candidate is retained with its identity, a stable reason and a nonempty diagnostic:
not compiled in, availability probe failure, initialization failure or window-kind conflict.
Count/index/copy routes expose that history without lending pointers into native storage.

Name parsing is a query: an unknown or retired name returns success with `recognized` false.
Selecting that same name is an operation and therefore fails. Retired names and numeric values are
diagnosed as retired and keep their reservations; they never alias a surviving renderer. The
selection reset route exists only so tests can return this process-wide state to its initial form.

The detailed build modes, latch point, fallback policy and window recreation rules are maintained
in [`../runtime-renderer-selection.md`](../runtime-renderer-selection.md).

## Assembly title

`cna_assembly_set_title_ext` copies the process-wide product title. It accepts an empty title but
rejects invalid UTF-8 and embedded NUL. The copy route follows the usual no-terminator,
no-partial-write contract. Set the title before the graphics device creates a window; changing the
stored default later does not rename an existing window.

The native module also has `AssemblyTitleAttributeEXT`, whose namespace-scope instance performs the
same assignment during C++ static initialization. C has no equivalent declaration form, so the C
mapping is the setter called from program startup; there is no attribute handle or C structure.

## The `CNAEXT` marker

`CNAEXT` expands to no behavior and therefore has no C mapping. It marks a C++ declaration as a CNA
extension rather than XNA 4.0. A route mapping an extension inside an otherwise XNA family normally
uses an `_ext` suffix. Entire CNA-owned families such as `core_ext.h` do not repeat that suffix on
every route, so suffix presence is not an exhaustive classifier.

## Verification

The principal tests are `CApi_AbiSmoke`, `CApi_ErrorSmoke`, `CApi_CoreExtSmoke`, all three
`CApi_RendererEnvironment_*` cases, `CApi_GraphicsExtSmoke` for the assembly title and the C++
`CApi_BoundaryDetail` test for exception translation. Generated route and Boolean-contract tests
cover null outputs, invalid identities, structure versions and the rule that only zero and one are
valid `CNA_Bool` inputs.
