# The CNA C ABI WebAssembly artifact

What a JavaScript or TypeScript consumer gets, and what it must know to use it. `fixcnats.md`
Phase 7 asks the artifact contract to define eleven things; this is that definition, and every
statement here was measured against a built module rather than inferred from the build flags.

The artifact is built by the `cna_c_api_wasm` target in an Emscripten tree and consists of
`cna_c_api.mjs` (an ES module factory) and `cna_c_api.wasm` beside it.

    const createCnaCApi = (await import('./cna_c_api.mjs')).default;
    const cna = await createCnaCApi();          // or ({ canvas }) for a game

## 1. Exported C ABI symbols

**2,874 names**: 2,872 `cna_*` routes plus `_malloc` and `_free`. Every route is prefixed with `_`
(`cna_get_abi_version` is `cna._cna_get_abi_version`).

The list is generated from the public headers by `tools/c-api/generate_wasm_exports.py`, not
maintained beside them, and the build rule depends on those headers -- a route added without the
list being regenerated was the defect `CABI-29` fixed. `CApi_WasmModuleSmoke` checks that every
generated name is a function on the instantiated module, so the list and the module cannot drift
apart silently.

## 2. Memory ownership

`_malloc` and `_free` are exported and are the only correct way to obtain a pointer to hand the
ABI. The caller owns every buffer it allocates and must free it; the ABI never takes ownership of a
caller pointer and never frees one.

Handle ownership is unchanged from the native ABI -- see `HANDLES.md` and `OWNERSHIP.md`. Nothing
about the WebAssembly build alters which handles are owned and which are borrowed.

## 3. UTF-8 string exchange

Two measured facts that a binding gets wrong by default:

- **Copied strings carry no terminator.** Every `..._copy_*` route writes exactly the bytes and no
  NUL, so `UTF8ToString(pointer)` reads past the end. Pass the length from the matching
  `..._get_*_size_*` route: `UTF8ToString(pointer, byteCount)`.
- **A `uint64_t` passed by value must arrive as a `BigInt`.** The module is linked with
  `WASM_BIGINT`, so `cna_platform_copy_current_name_ext(pointer, 4, out)` throws
  `Cannot convert 4 to a BigInt` -- a JavaScript `TypeError`, not a `CNA_Result`. It has to be
  `BigInt(4)`. Every route with a by-value 64-bit parameter is affected.

**Not pinned:** how a `CNA_StringView` passed *by value* is expanded in the wasm32 calling
convention. Reading one that the ABI passes *to* a callback is settled -- pointer at word 0,
`byte_length` at word 2, because the `uint64_t` is 8-aligned after the 4-byte pointer -- and the
browser probe depends on that. Passing one by value into a route was not established; use the
routes that take a pointer and a length where there is a choice.

## 4. Callbacks and 5. function tables

`addFunction` and `removeFunction` are exported and the table is growable
(`-sALLOW_TABLE_GROWTH=1`). Without them a consumer cannot construct a function pointer at all,
which is what made the artifact unusable for anything callback-driven until `CABI-41` -- and this
ABI is callback-driven throughout: game callbacks, `ContentLost`, storage disposing, resource
events.

A callback whose C signature takes a struct by value receives a **pointer** to it. The log sink,
`void(CNA_LogLevel, CNA_LogCategory, CNA_StringView, void*)`, is therefore `'viiii'`.

`CApi_WasmBrowserProbe` proves the round trip in a browser: a JavaScript function is registered
with `cna_logger_set_sink_ext` and invoked by compiled C with a real formatted message.

## 6. Canvas and window integration

The C ABI module itself needs no canvas -- it instantiates and answers without one, which is what
the probe does. A **game** needs one, and gets it the ordinary Emscripten way: pass the element to
the factory (`createCnaCApi({ canvas })`), or let SDL find `#canvas`.

Measured: `cna_demo_2d`, built from the same tree with `CNA_PLATFORM=SDL3` and
`CNA_GRAPHICS_RENDERER=WEBGL2`, brings up an 800x480 canvas with a live WebGL2 context in headless
Chrome, prints its EasyGL capability banner, and renders -- a screenshot of the canvas holds 23,775
distinct colours.

**Caveat worth having:** reading the default framebuffer with `gl.readPixels` after a frame returns
zeros unless the context was created with `preserveDrawingBuffer`. That looks exactly like "nothing
rendered". Screenshot the canvas instead.

## 7. Game loop

`cna_game_create` / `cna_game_run_one_frame` / `cna_game_destroy` are exported and are the loop. A
browser consumer drives `run_one_frame` from `requestAnimationFrame` rather than calling a blocking
`Run`, for the usual reason: the browser owns the event loop.

**Not pinned from JavaScript:** `cna_game_create` takes `CNA_GameCreateInfo` by pointer, and its
wasm32 layout differs from the native one recorded in `abi_baseline.json` (4-byte pointers). Hand
rolling that struct from JavaScript is how a binding gets `CNA_RESULT_INVALID_ARGUMENT` for no
visible reason. The layout is not published here because it was not measured; a binding should
derive it from a compiled probe rather than from the native baseline.

## 8. Shutdown

`cna_game_destroy` for a game; the matching `..._destroy` for every owned handle. Emscripten's own
teardown is not involved: the module is linked `--no-entry` and has no `main`, so nothing exits and
there is no `onExit` to hook.

## 9. Filesystem and title content

`FORCE_FILESYSTEM=1` is set and `FS` is exported, so a consumer can populate MEMFS before loading
content. Preloaded content works the ordinary Emscripten way -- `cna_demo_2d` ships a
`cna_demo_2d.data` package built by `--preload-file`.

`FS` was missing from the exported runtime methods until `CABI-41`, so this item had a build flag
and no reachable implementation.

## 10. ABI version query

`cna._cna_get_abi_version()` returns the packed `uint32`: major in bits 31..16, minor 15..8, patch
7..0. The current artifact answers **0.9.0**. A consumer must check this before trusting any other
route -- see `ABI_VERSIONING.md`, which lists the seven contract changes 0.9.0 makes.

## 11. Artifact provenance

`tools/c-api/generate_artifact_manifest.py` emits a JSON manifest recording source revision and
dirty flag, ABI version, target OS and architecture, renderer, audio backend, compiler and version,
build options, exported route count, SHA-256, build ID and size.

Its `status` is `BUILT` and nothing more, deliberately. The ladder above it -- `ABI_VERIFIED`,
`INTEGRATION_VERIFIED`, `PLATFORM_QUALIFIED`, `RELEASED` -- is something other measurements assert,
not something a manifest generator may claim about itself.

## What this artifact is not

`BUILT` and callable, with the C-level browser probe `fixcnats.md` Phase 7 asks for. It is **not**
`PLATFORM_QUALIFIED`: `cna-ts` does not track it (its audit still reports
`TRACKED_WASM_ARTIFACTS=0`), no consumer has been built against it, and the two "not pinned" items
above are real gaps a binding will hit. See `plans/plan_cabi.md` CABI-14, CABI-37 and CABI-41.
