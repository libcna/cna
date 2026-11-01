# The CNA C ABI WebAssembly artifact

This page defines the current Emscripten form of CNA's native C ABI and the evidence that protects
it. It is a browser-library artifact, not a JavaScript or TypeScript binding.

Configure CNA with `CNA_BUILD_C_API=ON` under Emscripten and build `cna_c_api_wasm`. The consumable
output is a pair in the target directory:

- `cna_c_api.mjs`, an ES-module factory named `createCnaCApi`;
- `cna_c_api.wasm`, the WebAssembly module loaded by that factory.

```javascript
const createCnaCApi = (await import('./cna_c_api.mjs')).default;
const cna = await createCnaCApi();          // Pass { canvas } when creating a game.
```

## Exports and ABI identity

Every declared public `cna_*` route is exported, together with `_malloc` and `_free`. Emscripten
spells a C route with a leading underscore on the module object, so `cna_get_abi_version` is
`cna._cna_get_abi_version`.

`tools/c-api/generate_wasm_exports.py` derives the link list from all public C headers. The custom
command depends on the generator and those headers, including newly added headers, so an
incremental build cannot silently retain an old list. Do not copy an export count into consumer
code or this document; `cna_c_api_wasm_exports.json` in the build tree is the generated source of
truth. `CApi_WasmModuleSmoke` instantiates the artifact and requires every generated name to be a
function on the module object.

Call `_cna_get_abi_version()` before trusting another route. It returns the same packed `uint32_t`
as the native library: major in bits 31..16, minor in 15..8 and patch in 7..0. The module smoke
reads `CNA_ABI_VERSION_{MAJOR,MINOR,PATCH}` from the public `abi.h` used by that build and requires
an exact match. Consumers must apply the compatibility policy in [ABI_VERSIONING.md](ABI_VERSIONING.md),
especially the stricter rule for the experimental `0.x` series, rather than pinning the version
printed in a dated document.

## Linear memory, strings and 64-bit values

Use the module's `_malloc` and `_free` for memory passed across the ABI. The caller retains and
frees its buffers; WebAssembly does not change the handle and pointer ownership rules in
[HANDLES.md](HANDLES.md) and [OWNERSHIP.md](OWNERSHIP.md).

Copied strings contain exactly the reported UTF-8 bytes and no trailing NUL. Pass the byte count
from the matching size query to `UTF8ToString(pointer, byteCount)`; the one-argument form can read
past the buffer.

The Emscripten artifact uses JavaScript `BigInt` for a `uint64_t` passed by value. For example, a
JavaScript `4` cannot stand in for C `UINT64_C(4)`; pass `4n` or `BigInt(4)`. This applies to every
by-value 64-bit length, tag and `CNA_Handle`.

The wasm32 lowering of a `CNA_StringView` passed by value from JavaScript into C is not a published
binding contract. A view delivered from C to the current callbacks has its pointer at word 0 and
its 64-bit byte length at word 2, and the browser probe measures that direction. Prefer routes
that accept a pointer and a separate length when writing a raw JavaScript consumer.

## Callbacks and the function table

`addFunction` and `removeFunction` are exported and `ALLOW_TABLE_GROWTH` is enabled, allowing a
consumer to create and retire function pointers for game, logging and resource callbacks. A C
callback parameter whose type is a structure passed by value is lowered by Emscripten to a pointer
to that structure. For example, the log sink
`void(CNA_LogLevel, CNA_LogCategory, CNA_StringView, void*)` uses the `addFunction` signature
`'viiii'`.

The WebGL2 browser probe exercises both directions: JavaScript installs a C log sink, and game
callbacks receive `CNA_Handle` values as JavaScript `BigInt` values.

## Canvas, renderer and game loop

The module itself does not require a canvas. `CApi_WasmModuleSmoke` deliberately instantiates it
under Node and calls only device-free routes. A game needs a canvas, supplied as
`createCnaCApi({ canvas })` or made available to SDL as `#canvas`.

The final target receives its renderer's Emscripten link contract from CNA:

| Renderer | Link contract applied by CNA |
|---|---|
| `WEBGL2` | exact `MIN_WEBGL_VERSION=2` and `MAX_WEBGL_VERSION=2` |
| `WEBGPU` and other non-WebGL renderers | this helper adds no WebGL-version option |

`cna_game_create`, `cna_game_run_frame_ext` and `cna_game_destroy` form the host-driven browser
loop. JavaScript owns `requestAnimationFrame` and calls `run_frame_ext`; unlike
`cna_game_run_one_frame`, that route includes the begin, exit and end-run lifecycle hooks.
`cna_game_run` is the blocking application-loop route and is not the normal browser-library entry
point.

The library target explicitly links with `ASYNCIFY=0`. Rewinding a JavaScript-entered exported
call can re-enter an i64-handle route without its original arguments. `CApi_WasmLinkContract`
checks the linked JavaScript rather than trusting link-option order, and the WebGL2 browser probe
requires each requested host frame to return exactly once without a page error or unhandled
rejection.

The probe reads the default framebuffer only for renderer operations; an external consumer that
calls `gl.readPixels` after presentation can receive zeros unless it created the context with
`preserveDrawingBuffer`. A canvas screenshot is the reliable presented-output check.

## Pinned wasm32 layouts

`modules/c-api/wasm/module_entry.c` compile-time-pins every public structure layout that the
browser regression writes directly in linear memory. These values describe this wasm32 artifact,
not a portable native ABI:

| Structure or member | Size or byte offset |
|---|---:|
| `sizeof(void*)` | 4 |
| `sizeof(CNA_GameCallbacks)` | 32 |
| `CNA_GameCallbacks::load_content` | 8 |
| `CNA_GameCallbacks::update` | 12 |
| `CNA_GameCallbacks::draw` | 16 |
| `sizeof(CNA_GameCreateInfo)` | 48 |
| `CNA_GameCreateInfo::target_elapsed_time_ticks` | 16 |
| `CNA_GameCreateInfo::window_title` | 24 |
| `CNA_GameCreateInfo::callbacks` | 40 |
| `sizeof(CNA_PresentationParameters)` | 44 |
| `sizeof(CNA_RenderTarget2DCreateInfo)` | 40 |
| `CNA_RenderTarget2DCreateInfo::format` | 20 |
| `CNA_RenderTarget2DCreateInfo::usage` | 32 |
| `sizeof(CNA_RenderTargetBinding)` | 24 |
| `CNA_RenderTargetBinding::render_target` | 8 |
| `CNA_RenderTargetBinding::array_slice` | 16 |
| `sizeof(CNA_UserPrimitives)` | 48 |
| `CNA_UserPrimitives::vertex_data` | 16 |
| `CNA_UserPrimitives::vertex_declaration` | 24 |
| `CNA_UserPrimitives::vertex_offset` | 32 |
| `sizeof(CNA_VertexPositionColor)` | 16 |

Bindings should generate their layouts from the public headers. The table documents the values
that the checked-in probe currently relies on; the static assertions are the enforcing source.

## Filesystem and shutdown

`FORCE_FILESYSTEM=1` is enabled and `FS` is exported, so a consumer can populate MEMFS before
loading content. Application targets may also use Emscripten's `--preload-file` support; that
packaging is separate from the C ABI module itself.

Destroy a game with `cna_game_destroy` and every other owned handle with its matching destroy or
release route. The module has no `main`: `--no-entry` makes it a library, so Emscripten process
exit and `onExit` are not its ownership mechanism.

## Automated evidence and its boundary

The three CTests are registered only when the C API and tests are enabled in an Emscripten build
and Node is available:

| Test | What it establishes |
|---|---|
| `CApi_WasmLinkContract` | no Asyncify runtime; an exact WebGL 2 request for a `WEBGL2` artifact |
| `CApi_WasmModuleSmoke` | module instantiation, exact header/runtime ABI identity, device-free calls, Web emulator identity and complete generated exports |
| `CApi_WasmBrowserProbe` | real browser loading plus callback, lifecycle and rendering behavior for the `WEBGL2` artifact |

The browser test is reported as skipped when Playwright is unavailable. It is also explicitly
skipped for a renderer other than `WEBGL2`: the current page asserts the WebGL2 renderer identity
and context and therefore is not evidence for a WebGPU C ABI artifact. WebGPU's renderer-level
browser evidence is documented separately in [../webgpu-renderer.md](../webgpu-renderer.md).

`tools/c-api/generate_artifact_manifest.py` currently describes native ELF and Mach-O shared
libraries only. It rejects a WebAssembly binary rather than mislabelling it as the host's OS and
architecture or reporting a false native-symbol count. There is no supported Wasm provenance
manifest yet; the module pair, generated export JSON, CMake cache, source revision and test results
must be retained together when qualifying a particular artifact.

The tests above qualify the stated artifact mechanics; they do not certify every browser, GPU,
renderer, threading mode or downstream binding. In particular, a normal WebGL2 consumer does not
need to add a WebGL-version override or enable Asyncify:

```text
MANUAL_WEBGL_VERSION_OVERRIDE_REQUIRED = false
MANUAL_ASYNCIFY_OVERRIDE_REQUIRED = false
```
