# CNA Native C API

## Status

The native C API is experimental at **ABI 0.52.0**. Its native library exports **3,213** `cna_*`
symbols through **60** public headers. Those headers have a measured C99 floor and compile as C++11
or later; the ABI uses fixed-width C representations rather than exposing CNA's C++ ABI.

The generated coverage inventory currently accounts for **8,134** runtime-scope public C++
declarations: **7,033 implemented**, **15 partially mapped**, **633 planned** and **453 not
applicable**. The experimental release verdict is therefore **Not ready**: the only unmet release
criterion is the 633 declarations that still need a C-native mapping or reviewed disposition.
These values are summaries, not a second status ledger; [`COVERAGE.md`](COVERAGE.md),
[`LIMITATIONS.md`](LIMITATIONS.md) and [`RELEASE_GATE.md`](RELEASE_GATE.md) are generated and remain
the sources of truth.

Native consumers install the `CNACApi` component and link `CNA::CApi`; supported Unix builds may
also provide `CNA::CApiStatic`. The installed shared and static consumers are exercised in
applicable native test trees. Emscripten instead produces the ESM plus WebAssembly package described
in [`WASM_ARTIFACT.md`](WASM_ARTIFACT.md). Start with [`CONSUMING.md`](CONSUMING.md) for a complete
C99 program and with [`LIMITATIONS.md`](LIMITATIONS.md) before deciding whether the current surface
covers an application's needs.

## Purpose

The C API exposes canonical CNA functionality to a C application without exposing C++ ABI details.
It lives in this repository and evolves atomically with the CNA modules it adapts.

```text
C program → CNA C API → CNA C++ → native platform
```

Sharp Runtime is a native C++ implementation dependency. A C consumer never includes its headers,
links its targets separately or receives one of its types across the ABI.

## Public-API coverage accounting

Every public declaration in a runtime module must receive an explicit disposition: implemented by
a tested C-native mapping, partially mapped with an approved boundary, planned under a live owner,
or not applicable for a recorded reason. C does not have C++ classes, overloads, exceptions,
templates or inheritance, so callable coverage uses these representations:

| C++ concept | C API representation |
|---|---|
| Value type | Fixed-layout `CNA_*` POD struct and functions where needed. |
| Resource/object | Validated `CNA_Handle` plus create/query/action/release functions. |
| Constructor/static factory | `cna_<type>_create` or `cna_<type>_create_<variant>`. |
| Property | `cna_<type>_get_<property>` and `cna_<type>_set_<property>`. |
| Overload | A stable descriptive suffix, never C++ overload resolution. |
| Event/delegate | C function pointer, opaque context and registration handle. |
| Collection | Count/query/copy or stable element-handle operations. |
| Exception | `CNA_Result` and structured per-thread error information. |
| Stream/task | CNA-neutral callback or operation handle. |

[`COVERAGE.md`](COVERAGE.md) is the compact, hashed status record. The reviewed mappings live in
`tools/c-api/coverage_mappings.json`; a public CNA symbol with no generated mapping is incomplete,
even if a similar operation happens to work indirectly.

The matrix is generated with Doxygen's C++ parser, not maintained by hand. `--write` refreshes the
small tracked summary and writes the complete per-symbol matrix to the ignored
`build/c-api/COVERAGE_FULL.md` artifact:

```bash
python3 tools/c-api/generate_coverage_inventory.py --write
python3 tools/c-api/generate_coverage_inventory.py --check
```

`tools/c-api/coverage_mappings.json` records reviewed mappings to the existing C API. Unmapped
callable declarations remain `planned`; useful but incomplete routes remain `partial`; explicitly
deleted C++ operations are retained as `not applicable` so the source inventory stays complete
without inventing a C operation. `--check` is a mandatory gate (CBIND-043): the CTest test
`CApiCoverageMatrix` and the `c-api-coverage-gate` workflow both fail on an unmapped public symbol.

Each rule also carries `approved_symbols`, the stable IDs it was reviewed against, and covers a
symbol only when its patterns **and** its approved set match. Many rules are deliberately broad —
`.*` over one header, asserting that the header's whole contract is bound — and without the second
half a later merge inherits "implemented and tested" for routes that do not exist. So a newly added
declaration always surfaces as `planned`, whatever rule its name happens to fall under. After the
new symbols have actually been bound or dispositioned, re-record the approvals deliberately:

```bash
python3 tools/c-api/generate_coverage_inventory.py --approve-rule-symbols
```

This is a separate mode on purpose. The routine `--write` a developer runs to refresh the inventory
must not be able to widen a rule's authority (CBIND-050).

### Route presence and execution evidence

`CApiRouteTestCoverage` runs `tools/c-api/check_route_test_coverage.py --check`. Its
ratchet measures **declaration/name presence**: exported header names mentioned in C/C++
test/example source. Comments, string literals and literal `#if 0` branches are excluded;
unknown conditions, build inclusion and macro expansion are not evaluated. An unused function,
address reference or generated refusal test can satisfy this census. It cannot prove a route
was called, an assertion ran or the required semantic categories were exercised. The unchanged
zero-uncovered budget is a source maintenance gate, not runtime coverage.

The tool prints separate declaration presence, configured registration, actual execution and
runtime qualification layers. Optional `--evidence-report layers.json` retains these as JSON.
For registration candidates, pass `--configured-inventory configured.json`, produced by
`ctest --test-dir cmake-build-software -N --show-only=json-v1`. Disabled entries do not satisfy a
required registration; an aggregate gtest filter identifies only potential cases.

For actual execution, retain runs using [the CTest evidence adapter](../execution-evidence.md),
then join them explicitly:

```bash
python3 tools/c-api/check_route_test_coverage.py --check \
  --required-ctest '<configured semantic CTest name>' \
  --execution-record /path/to/retained-run/record.json \
  --evidence-report /tmp/c-api-evidence-layers.json --require-execution
```

Repeat `--required-ctest` for **every required category** under review and `--execution-record`
for its captures. There is no inferred route-to-runtime-test mapping. The execution requirement
fails without explicit witnesses, when any witness is absent/disabled/skipped/not run, or when
artifacts fail validation. Required C API witnesses need runtime-kind records; metadata-kind
captures and entries labelled `metadata`/`declaration-presence` cannot substitute. Aggregate
gtest witnesses also require exact RUN/OK output, so zero selected gtest cases cannot pass this
layer. Captures must share source/build/configuration identity; an explicitly supplied inventory
must match the retained one. Captures must also share their full configured inventory. Record
paths, hashes, identities and errors are retained in the JSON report.

**A successful execution requirement is still not runtime qualification.** CTest counts entries,
not assertions or C route calls. An empty test body can pass; `executed_assertions` remains
unknown and `runtime_qualification.status` stays `not-established`. Captures describe their
recorded snapshot, not proof that current binaries were rebuilt or dirty source bytes stayed
unchanged. Review semantic assertions and independent observations separately. The default CTest
is labelled `metadata;declaration-presence` and needs no execution artifacts. Its regression
suite is `CApiRouteTestCoverageToolTests` (CPU only).

### Admitting a new C route

Before adding a family or function, establish:

1. the concrete C use case and canonical CNA C++ operation;
2. a signature made only of fixed-width values, POD structures, handles and C callbacks;
3. ownership, nullability, parent lifetime and thread affinity;
4. representations for text, collections, buffers, paths, streams, callbacks and errors;
5. exception translation that exposes no C++ or Sharp Runtime type;
6. renderer/platform capability behavior;
7. whether an existing C representation already expresses the same operation;
8. whether the change is additive under the current ABI versioning rules.

Do not map language mechanics merely because Doxygen reports them. Prefer an existing C
representation when it already carries the same data, and collapse overloads only when the
canonical implementation itself collapses them. Read the implementation as well as the header;
validation order, clamps, ownership and no-op behavior are often visible only there.

Every new route needs all applicable layers in the same change:

- real-C header compilation and a strict-C call through the public library;
- semantic adapter coverage, including conversion and exception behavior;
- null, invalid, stale, wrong-kind, buffer, encoding, enum and lifetime refusals;
- a real renderer observation for any draw claim;
- ABI/export/layout/version evidence.

Regenerate coverage after the change and verify that the implemented delta matches the reviewed
symbols. `planned_owners` in `tools/c-api/coverage_mappings.json` contains only live owner metadata;
the generated [`COVERAGE.md`](COVERAGE.md) is the current backlog and measured count. Completed
campaign chronology belongs in Git history, not in a second status document.

## Public naming and language baseline

- Public types use the `CNA_` prefix; public functions use lowercase `cna_`.
- C API public headers live under `CNA/C/`; `CNA/C/cna.h` is the umbrella header.
- Public headers need only C99 and must also compile as C++11 and later for mixed-language
  consumers; [`COMPATIBILITY.md`](COMPATIBILITY.md) records the compilers and modes that hold them
  to it.
- All fallible functions return `CNA_Result`; successful values use out parameters.
- Headers include only standard C headers and use no C++ or Sharp Runtime spelling.
- Every public declaration receives a Doxygen block in its `.h` header.

The authoritative header set is `modules/c-api/include/CNA/C/*.h`; it currently contains 60 files,
with `cna.h` as the umbrella. The compatibility gate discovers that directory and compiles every
header independently, so a new family cannot be hidden by a manually maintained list here.

## Supported configurations

The C API uses CNA's renderer registry and selection state; it does not maintain a parallel
renderer mechanism. A single-renderer build exposes one identity, while a multi-renderer build can
enumerate its compiled identities, select a preferred renderer and report the renderer that
actually created the device. Programs query capabilities, detailed features, numeric limits and
per-format support from the active borrowed graphics device rather than inferring them from an
identity. [`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md) defines those distinctions.

Runtime evidence is configuration-specific. `HEADLESS` is the deterministic lifecycle/state and
honest-refusal control, not proof that drawing works. Pixel, presentation and other native-renderer
claims require focused observations on a capable renderer. Unsupported optional behavior returns
`CNA_RESULT_NOT_SUPPORTED`; [`COMPATIBILITY.md`](COMPATIBILITY.md) records exactly what the header
matrix proves and deliberately does not promote that evidence into renderer or platform coverage.

## Hard boundaries

The C API must not expose:

- C++ class pointers, references, exceptions, RTTI, templates, `std::*` or compiler-specific ABI;
- `System::*`, Sharp Runtime object layouts, streams, tasks, exceptions, delegates or collections;
- renderer implementation objects or native graphics-device ownership. The versioned borrowed
  native-window descriptor is a narrow interop exception, not a renderer object exposed as API;
- hidden ownership, C++ containers, locale-dependent strings or implicit allocator rules.

## Document map

### Consumer contracts

| Question | Contract |
|---|---|
| How do I install, link and run it? | [`CONSUMING.md`](CONSUMING.md) |
| Where are the ABI primitives, diagnostics, logging and platform facts? | [`CORE.md`](CORE.md) |
| What is missing or deliberately narrower? | [`LIMITATIONS.md`](LIMITATIONS.md) |
| What may change between ABI versions? | [`ABI_VERSIONING.md`](ABI_VERSIONING.md) |
| What owns a handle or resource, and when does it die? | [`HANDLES.md`](HANDLES.md), [`OWNERSHIP.md`](OWNERSHIP.md) |
| How are failures reported? | [`ERRORS.md`](ERRORS.md) |
| How do text, buffers and callbacks cross the boundary? | [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md), [`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md) |
| How do renderer selection and capability queries work? | [`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md) |
| Why do no Sharp Runtime types cross the ABI? | [`SHARP_RUNTIME_BOUNDARY.md`](SHARP_RUNTIME_BOUNDARY.md) |

### Subsystem contracts

| Area | Contracts |
|---|---|
| Graphics resources and devices | [`GRAPHICS_RESOURCES.md`](GRAPHICS_RESOURCES.md), [`GRAPHICS_DEVICE.md`](GRAPHICS_DEVICE.md), [`GRAPHICS_EXT.md`](GRAPHICS_EXT.md) |
| Effects, models and animation | [`EFFECTS.md`](EFFECTS.md), [`MODELS.md`](MODELS.md) |
| Textures and buffers | [`TEXTURES.md`](TEXTURES.md), [`TEXTURE_VOLUMES.md`](TEXTURE_VOLUMES.md), [`VERTEX_BUFFERS.md`](VERTEX_BUFFERS.md), [`INDEX_BUFFERS.md`](INDEX_BUFFERS.md) |
| Values and input | [`MATH_AND_3D_VALUES.md`](MATH_AND_3D_VALUES.md), [`INPUT_SNAPSHOTS.md`](INPUT_SNAPSHOTS.md) |
| Content and storage | [`CONTENT.md`](CONTENT.md), [`CNB.md`](CNB.md), [`STORAGE.md`](STORAGE.md) |
| Network and services | [`NET.md`](NET.md), [`GAMER_SERVICES.md`](GAMER_SERVICES.md), [`GAME_COMPONENTS.md`](GAME_COMPONENTS.md) |
| Audio, media and devices | [`AUDIO.md`](AUDIO.md), [`MEDIA.md`](MEDIA.md), [`DEVICES.md`](DEVICES.md) |

### Maintainer evidence

| Evidence | Document |
|---|---|
| Generated source-to-C accounting | [`COVERAGE.md`](COVERAGE.md) |
| Header compilers and language modes | [`COMPATIBILITY.md`](COMPATIBILITY.md) |
| Byte-facing oracle and fuzz boundary | [`FUZZING.md`](FUZZING.md) |
| Measured experimental release verdict | [`RELEASE_GATE.md`](RELEASE_GATE.md) |
| Generated versus hand-written route implementation | [`GENERATED_ROUTES.md`](GENERATED_ROUTES.md) |
| Native WebAssembly package | [`WASM_ARTIFACT.md`](WASM_ARTIFACT.md) |
