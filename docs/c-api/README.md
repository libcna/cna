# CNA Native C API

## Status

The native C API is experimental. Its initial `0.1.0` shared library and public C17 headers provide
the ABI/error substrate plus a HEADLESS- and SDL_RENDERER-tested `Game` lifecycle slice
(`CNA_GameTime`, clear, UTF-8 title and callback-scoped graphics-device capability discovery) plus
owned Color-format `Texture2D` creation, bulk RGBA8 transfer, POD-array `SpriteBatch` submission and
point-in-time keyboard, mouse, gamepad and touch snapshots. An owned `ContentManager` adds UTF-8
root/cache control and a typed Color Texture2D load route; owned PCM16 sound effects add explicit
instance playback/control plus a real native playback-availability probe. Exact support and
omissions are recorded in
[`FEATURE_MATRIX.md`](FEATURE_MATRIX.md). It is not complete public CNA coverage. The contract in
this directory is binding on implementation until the release gate in
[`../../plan_binding.md`](../../plan_binding.md) is complete.

## Purpose

The C API exposes canonical CNA functionality to a C application without exposing C++ ABI details.
It lives in this repository and evolves atomically with the CNA modules it adapts.

```text
C program → CNA C API → CNA C++ → native platform
```

Sharp Runtime is a native C++ implementation dependency. It is never a C API dependency or a
public C type.

## Complete public-API coverage

The finished C API must cover every public CNA C++ type, constructor, property, method overload,
operator, constant and event. C does not have C++ classes, overloads, exceptions, templates or
inheritance, so coverage uses a documented C-native equivalent:

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

[`COVERAGE.md`](COVERAGE.md) is the required source-to-C mapping record. A public CNA symbol with
no row is incomplete, even if a similar operation happens to work indirectly.

The matrix is generated with Doxygen's C++ parser, not maintained by hand:

```bash
python3 tools/c-api/generate_coverage_inventory.py --write
python3 tools/c-api/generate_coverage_inventory.py --check
```

`tools/c-api/coverage_mappings.json` records reviewed mappings to the existing C API. Unmapped
callable declarations remain `planned`; useful but incomplete routes remain `partial`; explicitly
deleted C++ operations are retained as `not applicable` so the source inventory stays complete
without inventing a C operation. CBIND-043 will promote `--check` to a mandatory configured/CI
gate.

## Public naming and language baseline

- Public types use the `CNA_` prefix; public functions use lowercase `cna_`.
- C API public headers live under `CNA/C/`; `CNA/C/cna.h` is the umbrella header.
- Initial headers require C17 and must also compile as C++23 for mixed-language consumers.
- All fallible functions return `CNA_Result`; successful values use out parameters.
- Headers include only standard C headers and use no C++ or Sharp Runtime spelling.
- Every public declaration receives a Doxygen block in its `.h` header.

The exact modules and headers are defined in the implementation plan. The current header split is
`abi.h`, `core.h`, `runtime.h`, `graphics.h`, `input.h`, `content.h` and `audio.h`; later family
headers follow as coverage requires.

## Supported configurations

The C API shares CNA's compile-time renderer selection. It does not add a second renderer
selection mechanism. A C program can query the selected renderer, graphics features, touch-device
connection and native audio-playback availability. An unsupported operation returns
`CNA_RESULT_NOT_SUPPORTED` instead of silently changing behavior.

The automated vertical slice runs with HEADLESS for deterministic state and honest unsupported
readback behavior. The same strict-C source runs under SDL_RENDERER and verifies exact texture,
SpriteBatch, clear and backbuffer pixels rather than inferring rendering correctness from calls.

## Hard boundaries

The C API must not expose:

- C++ class pointers, references, exceptions, RTTI, templates, `std::*` or compiler-specific ABI;
- `System::*`, Sharp Runtime object layouts, streams, tasks, exceptions, delegates or collections;
- renderer-private native objects such as SDL windows, graphics devices or native API handles;
- hidden ownership, C++ containers, locale-dependent strings or implicit allocator rules.

See the sibling contract documents for ABI versioning, handles, ownership, errors, text/buffers,
callbacks/threading, renderer capability reporting, [input snapshots](INPUT_SNAPSHOTS.md) and the
Sharp Runtime boundary. The owned content/cache contract is in [`CONTENT.md`](CONTENT.md). The
[audio ownership/control contract](AUDIO.md) defines the PCM and mixer-thread boundary. The
[initial feature matrix](FEATURE_MATRIX.md) is the concise consumer view;
[`COVERAGE.md`](COVERAGE.md) is the source-to-C implementation record.
