# CNA C API ABI and Versioning Contract

## ABI identity

The current experimental ABI is **0.60.0** (MTH-CABI-009).

| Added family | C API listing |
|---|---|
| Owner-contract closure routes (0.60) | `cna_network_gamer_get_shared_machine_ext`, `cna_network_gamer_set_shared_machine_ext`, `cna_game_window_set_logical_size_provider_ext`; shared native identity and retained logical-size callback |
| Native closure routes (0.59) | Native gamer default cursor; effect/component cursors with mutation refusal and cached Current; independently owned retaining component Current views; live mutable vertex-declaration views, assignment and disposal |
| Content route additions (0.58) | `content_values.h`: deferred foreign-reference fixups with lifetime pins; base snapshots and abstract identities; closed content-value snapshots, target associations/raw reads; CPU byte-source/resource managers and uncached disposable recording |
| Service route additions (0.57) | Gamer virtual names; dictionary pair Add/Contains/Remove; copied deployment configuration; file/text drop registrations and dispatch; primary selection and copied MIME clipboard payloads |
| Model schema 2 | `cnb_model_v2.h`: frozen identities, versioned geometry/graph descriptors, immutable CPU snapshot handles, copied reads, native equality, encode/decode |
| CNB CPU helpers | `cna_cnb_sound_effect_schema_version_for_format`, `cna_cnb_sound_effect_data_copy_pcm16`, `cna_cnb_texture_data_generate_rgba8_mips` |
| CNB filesystem paths | Four existing import/build routes explicitly convert UTF-8 to native paths; Windows qualification remains open |
| Sampler/profile | `cna_sampler_state_collection_get_active_count_ext`; `cna_texture_is_volume_format_allowed_by_profile_ext` |
| Buffer runtime names | `cna_{index,vertex}_buffer_{get_runtime_type_name_byte_count,copy_runtime_type_name}_ext` |
| Shader identities/diagnostics | `CNA_ShaderLanguageEXT`, `CNA_ShaderStageEXT`, `CNA_ShaderDiagnosticSeverityEXT`; `shader.h` snapshot and identity routes |
| Typed graphics transfers | `cna_texture{3d,cube}_create_format_ext`, `cna_texture{3d,cube}_{set,get}_data_typed_ext`; index source-offset/raw and backbuffer raw readback |
| Presentation/drawing | `cna_graphics_device_present_region_ext`; `cna_graphics_device_draw_instanced_primitives_base_instance_ext` |
| Shader packages | `shader.h` code/package/selection snapshots; `effects.h` typed code/package/fallback construction and compiler-diagnostic snapshots |

The library exports 3,397 symbols. Existing layouts, base buffer names and Color-only creation
contracts are unchanged. New
snapshot handles have typed destruction; child code is independently copied. Device runtime
variants are separate qualification tests. Public headers define the detailed contracts.

## Behaviour corrections within ABI 0.60

No route, layout or enum identity changes and no new symbols. Existing routes follow XNA under the
experimental minor-version rule (owner decisions):

- TQ-036: the `cna_touch_state` mutation routes return `CNA_RESULT_NOT_SUPPORTED` and write nothing,
  because XNA's TouchCollection is read-only. `cna_touch_state_init` gives a disconnected snapshot, and
  `cna_touch_state_init_from_locations` a connected one, even with no locations. The pure gamepad
  dead-zone helpers quantize sticks to short samples and apply XNA's integer thresholds.
- TQ-035: after `cna_graphics_resource_dispose`, an occlusion query keeps XNA's state machine:
  `cna_occlusion_query_get_is_complete` and `cna_occlusion_query_get_is_pixel_count_precise_ext`
  succeed with `CNA_FALSE`, while pixel count, begin and end still return `CNA_RESULT_INVALID_STATE`.
  A NullReferenceException from any route maps to `CNA_RESULT_INVALID_STATE` rather than
  `CNA_RESULT_INTERNAL`. Disposing subscriptions on effect and occlusion-query handles are never
  invoked. A render target's ContentLost flag survives binding; a draw or SpriteBatch flush into
  it, a successful clear while it is bound, or a completed SetData clears it.
- TQ-035: `cna_sprite_effect_create` effects expose `MatrixTransform`, set on every apply to FNA's
  viewport projection (M33 = -1), and clones keep it. `cna_texture2d_create_from_encoded_memory`
  (non-DDS) gives texels whose alpha is 0 an RGB of 0, after any requested resize.
  `cna_effect_material_create` and the base `cna_effect_clone` of a stock effect keep its
  techniques, passes and parameter values instead of one "Default" technique; texture values are
  shared references. Such a copy draws with the stock effect's shaders, fed its own raw parameter
  values with no stock OnApply; an apply whose ShaderIndex selects none of the stock shaders returns
  `CNA_RESULT_INVALID_STATE` (HND-TQ-25 D9).
- HND-TQ-25 D8: `cna_effect_material_create` also retains every C texture handle its source retains
  at that moment -- the stock texture slots and each parameter's slots, through elements and
  structure members -- until the material replaces or clears it or is destroyed. Destroying such a
  texture while only the material holds it returns `CNA_RESULT_INVALID_STATE`, as for its source.
  `cna_effect_clone` does not extend the source's C retention to the copy.
- TQ-037: `cna_game_run_one_frame` only ticks, as XNA's RunOneFrame does: it does not initialize
  the game or begin a run, so a host that needs initialization calls `cna_game_run_frame_ext`.
  Disposing the graphics device manager removes only its graphics-device service. A changed graphics
  profile or a repeated device creation replaces the device, so a borrowed device handle taken
  before it names the disposed predecessor. BeginDraw refuses an inactive device in applied fullscreen.
- TQ-039: the `_to` send routes refuse `CNA_INVALID_HANDLE` as a recipient with
  `CNA_RESULT_INVALID_ARGUMENT` (`System.ArgumentNullException`); the routes without a recipient are
  the broadcast. Sending an empty packet writer is refused (`System.ArgumentOutOfRangeException`) and
  sends nothing. A local or system-link gamer added with `cna_network_session_add_local_gamer` joins
  at the next update, and the session's host is null after the host leaves until a host change.

## ABI 0.60 compatibility notes

Three additive routes bring the surface to 3,397 exports. No existing route, layout or enum identity
changes. `CNA_GameWindowLogicalSizeProviderEXT` is a new callback typedef; its failure channel follows
lifecycle callbacks, with a separate availability boolean for a declined size. Existing machine
copy/set routes preserve value semantics. Shared aliases have independent session/roster pins and
use the existing typed machine release. Null shared assignment is refused; assign a distinct empty
machine to detach from an identity. Two explicit-handle component lifetime subsets and two
transport-managed no-C-form rows are owner-approved.
[CLOSURE_060.md](CLOSURE_060.md) records the exact rows and the callback lifetime.
The four Windows qualification rows remain planned; the experimental release stays Not ready.

## ABI 0.59 compatibility notes

Sixteen additive routes were introduced in ABI 0.59; existing layouts and symbols stay fixed.
Two existing route behaviors are corrected under the experimental minor-version rule:
`cna_gamer_enumerator_get_current` succeeds with a null handle before start/after exhaustion
and preserves cached Current after collection mutation; MoveNext/Reset refuse native version
changes. `cna_sound_effect_create_from_encoded_ext` uses the System::IO::Stream strict PCM WAV
factory, so non-WAV formats that the std::istream extension accepted are refused.

## ABI change ledger

Every generation moves the minor, additive ones included, so a consumer has a number to require.
Kinds: *additive* (new names only); *range* (a closed enumerable range grew, so its `MAXIMUM`
moved); *rule* (an existing route's behaviour or error rule changed); *break* (a name, value,
layout or export was removed or changed). The task IDs are decoded in
[`../task-id-index.md`](../task-id-index.md).

| ABI | Change | Kind |
|---|---|---|
| 0.59–0.55 | The families listed under *ABI identity*: native closure routes (0.59), content (0.58), service (0.57), and the graphics, shader, sampler and CNB families before them | Additive; 0.59 also corrects two rules (above) |
| 0.54.0 | Gamepad value strings; packed-vector and display-mode hash/strings; graphics-device-information equality/hash; `CNA_GraphicsDeviceTypeEXT` with adapter queries; `cna_graphics_device_get_logical_size_ext`; input-emulation switches; `cna_project_graphics_profile_{get,set}_ext` | Additive |
| 0.53.0 | Static profile-legality queries `cna_texture_is_{,cube_,render_target_}format_allowed_by_profile_ext`: explicit profile and format, no device, any thread | Additive |
| 0.52.0 | `cna_content_manager_load_video`: an independently owned Video from `ContentManager.Load<Video>` | Additive |
| 0.51.0 | Renderer identity `SDL_GPU` renamed `SDLGPU`; `CNA_GRAPHICS_RENDERER_SDLGPU` keeps value `31`, the old name is not an alias and the textual `SDL_GPU` selector is rejected | Break (source) |
| 0.50.0 | Skinned-model part diffuse colour routes; `CNA_DisplayColorSpace` types the three display-colour-space routes again (same `uint32_t` signatures); CNB model cameras (`CNA_CNB_MODEL_CHUNK_CAMERAS`, `CNA_CnbModelCamera`, five routes); renderer features `32`--`36`, `CNA_RENDERER_FEATURE_MAXIMUM` `36` | Additive, range |
| 0.49.0 | The renderer classification surface uses the renderer vocabulary: `CNA_GraphicsRenderer{Category,Maturity}`, `CNA_GRAPHICS_RENDERER_{CATEGORY,MATURITY}_*` and eight `cna_graphics_renderer_*` routes replace the former names; values, signatures and answers unchanged, no aliases | Break (source and binary) |
| 0.48.0 | Renderer identities `SDL_RENDERER` (`1`), `VULKAN` (`8`), `DIRECTX11` (`14`) and `METAL` (`42`) retired; their values are reserved and refused | Break (source) |
| 0.47.0 | The retired modern GPU layer's C surface removed: compute/indirect capabilities, renderer features `18`--`20` and `23`, compute/storage/timer limits, storage format-usage bits, indirect-draw structures and routes, compute work-group queries; surviving numbers unchanged, retired ones reserved | Break |
| 0.46.0 | `CNA_NATIVE_WINDOW_SYSTEM_UIKIT` (`9`): iOS/iPadOS report SDL's `UIWindow*` | Range |
| 0.45.0 | Renderer identities `OPENGLES2` (`2`), `WEBGL1` (`5`), `DIRECTX12` (`15`) and `CANVAS` (`17`) retired | Break (source) |
| 0.44.0 | The activated/deactivated handlers run inside a callback scope and can borrow the game's device | Rule |
| 0.43.0 | `cna_sound_effect_play` and `_play_with_settings` answer on any thread, as XNA's `SoundEffect.Play` did | Rule |
| 0.42.0 | `cna_game_get_graphics_device` lends the device before the run begins, on the creating thread | Rule |
| 0.41.0 | `cna_game_run_foreign_thread_calls_ext`: run queued foreign-thread calls now | Additive |
| 0.40.0 | The twelve `cna_graphics_adapter_*` routes also accept the active game's own handle | Rule |
| 0.39.0 | `cna_game_set_foreign_thread_calls_ext`: opt-in queuing of other threads' calls to the game thread | Additive |
| 0.38.0 | `cna_game_run_frame_ext`: a host-driven run, one frame per call, that begins and ends the run | Additive |
| 0.37.0 | Four routes naming the .NET exception type and parameter behind a thread's last failure | Additive |
| 0.36.0 | `cna_packet_reader_copy_data_ext` | Additive |
| 0.35.0 | `next`'s 0.33.0 merged with the gamer-services line's 0.34.0 under a number neither used; within 0.35.0, `CNA_CNB_SOUND_EFFECT_SCHEMA_VERSION` was corrected from `1` to `2` (the schema the library already wrote; owner-approved, BINDFIX-048) | Rebuild |
| 0.34.0 (gamer-services line) | `cna_guide_set_is_visible` and `cna_guide_set_is_trial_mode` removed (XNA keeps them internal) | Break |
| 0.33.0 (gamer-services line) | The standard avatar types render for real; the Avatar real-rendering extension (eleven routes, one structure) removed; `CNA_AvatarRendererInfo` keeps its size | Break |
| 0.33.0 (`next`) | Renderer identities `HTML_DOM` (`18`), `OPENGL4` (`33`), `GDI` (`40`) and `SVG_DOM` (`44`) retired; `CNA_GRAPHICS_RENDERER_MAXIMUM` `43` | Break (source) |
| 0.32.0 | Achievement and gamer-profile picture copy routes; Guide/Dispatcher routes reflect configured service availability and real authentication | Additive, rule |
| 0.31.0 | Renderer identities `DIRECT2D` (`16`), `FREEDIRECT` (`21`) and `PORTABLEGL` (`46`) retired; maximum `44` | Break (source) |
| 0.30.0 | `engine_layer.h` with its routes, handles, enums and layouts removed; `graphics_ext.h` keeps ASCII, CRT, colour-depth and DebugDraw | Break |
| 0.29.0 | The SpriteBatch 2D mesh route and its versioned structure removed: no renderer implemented it, so every caller already got `CNA_RESULT_NOT_SUPPORTED` | Break |
| 0.28.0 | Twenty-five renderer identities retired; maximum `46` | Break (source) |
| 0.27.0 | `CNA_GRAPHICS_RENDERER_RLGL` (`51`) | Range |
| 0.26.0 | `CNA_RENDERER_FEATURE_BASE_INSTANCE_DRAWING` (`31`) | Range |
| 0.25.0 | `CNA_SHADER_DIALECT_SPIRV` (`7`): already-compiled SPIR-V distinguished from Vulkan GLSL source | Range |
| 0.24.0 | `CNA_RENDERER_FEATURE_TEXTURE_3D_SAMPLING` (`30`), separate from volume storage | Range |
| 0.23.0 | `cna_decal_pass_is_supported` | Additive |
| 0.22.0 | Ten renderer identities restored with their former values | Range |
| 0.21.0 | `cna_environment_get_device_type`; object-dictionary logical type-name pair | Additive |
| 0.20.0 | Eleven renderer identities removed; their values are not reused | Break (source) |
| 0.19.0 | `Load<Model>` and the content `Tag` its processor wrote (three routes) | Additive |
| 0.18.0 | The content-processor `Dictionary<string, object>` (`CNA_ObjectDictionaryHandle`) and the reference-shaped reflective reader | Additive |
| 0.17.0 | Reflective content readers (`CNA_ReflectiveTypeReaderBuilderHandle`) | Additive |
| 0.16.0 | Options-carrying raw vertex uploads, the `EffectMaterial` retained-texture pair, the occlusion-query precision flag | Additive |
| 0.15.0–0.10.0 | The `.cnb` container (0.10), document, cursor and writers (0.11), texture (0.12), model (0.13) and sprite-font/audio/curve/animation schemas (0.14), loader registry and compilation front ends (0.15) | Additive |
| 0.9.0 | Owned GraphicsDevice routes, render-target ContentLost subscription and video frame identity; contract changes: `Apply3D` refuses on a playing instance never positioned and accepts any positive listener count, non-finite sprite values and unnamed `SpriteSortMode` values are accepted (the latter run as `Deferred`), ContentLost is raised, video frame generations never restart | Additive, rule |
| 0.8.0 | NanoVG identity and five engine-layer capability identities | Range |
| 0.7.0 | Six PBR and morph-target routes | Additive |
| 0.6.0 | Empty shader source refused uniformly | Rule |
| 0.5.0 | Portable native-window handle routes | Additive |
| 0.4.0 | `cna_content_manager_register_cnj_loader_ext` | Additive |
| 0.3.0 | Every route taking a `CNA_Bool` refuses a byte outside {0, 1} | Break |
| 0.2.0 | Routes of CBIND-054..058 | Additive |
| 0.1.0 | Initial version | — |

A retired renderer identity's value is permanently reserved and never assigned to another renderer;
a new identity takes the next never-assigned value, **`52`**, not `MAXIMUM + 1`.
`scripts/check_renderer_identities.py` fails the build if a retired value or name is reused, and
[`../removed-renderers.md`](../removed-renderers.md) lists every retired identity and its value.

## Version encoding and compatibility

The packed representation is a `uint32_t`:

```text
bits 31..16  major
bits 15..8   minor
bits  7..0   patch
```

The public header provides `CNA_ABI_VERSION_ENCODE(major, minor, patch)`,
`CNA_ABI_VERSION_MAJOR`, `CNA_ABI_VERSION_MINOR`, `CNA_ABI_VERSION_PATCH`, and
`cna_get_abi_version()`. A consumer must reject a different major. From `1.0` on it may then require
a minimum minor and accept every later one, because later minors are only additive. While the
major is `0` that is not enough: under the `0.x` rule below any minor may break, so a `0.x`
consumer must also reject a minor it has not explicitly reviewed and may accept a later patch of
that minor.

The installed CMake package checks only the first half: its version file is `SameMajorVersion`, so
`find_package(CNA 0.1 CONFIG)` accepts `0.1` and every later `0.x`, and rejects a `1.x`. Under `0.x`
the consumer's own runtime check against `cna_get_abi_version()` is what refuses a newer minor;
`modules/c-api/examples/c/hello_cna.c` shows it.

ABI `0.x` is experimental: an incompatible change requires owner review, a minor-version
increment, release notes and a regenerated ABI baseline. ABI `1.x` and later permit only additive,
backward-compatible changes within a major. From `1.x`, removing or changing an existing function,
numeric constant, struct field, field meaning, ownership rule, error rule or callback rule requires
a new ABI major.

### What a `CNA_Bool` outside {0, 1} does

**Every route refuses it**, with `CNA_RESULT_INVALID_ARGUMENT`, naming the parameter. A route on a
surface a given build compiled out answers `CNA_RESULT_NOT_SUPPORTED` instead, because refusing an
argument to a route that does not exist there would be the wrong answer.

Before `0.3.0` some routes read a non-canonical byte as true and others as false (`CBIND-066`).
The guarantee is held by a **generated** test rather than by a hand-maintained route list
(`tools/c-api/generate_bool_contract_test.py`, run as `CApi_BoolContractSmoke`), so a route declared
with a new flag parameter is covered the moment it is declared. The generator errors on a by-value
parameter type it has no stand-in for rather than dropping that route, so the covered set cannot
shrink quietly.

## Naming and linkage

The public export macro is named `CNA_C_API`. On Windows it expands to `__declspec(dllexport)`
while building the C API and `__declspec(dllimport)` for a shared-library consumer. On ELF and
Mach-O targets it uses default visibility while building the library and has no import annotation
for consumers. Static consumers use the documented `CNA_C_API_STATIC` definition.

Every exported function has C language linkage in its C++ implementation and uses the `cna_`
prefix. `CNA_*` is reserved for types, constants, macros and compile definitions. Public symbols
must appear in the checked export allowlist; the C++ implementation and Sharp Runtime symbols are
not ABI promises.

## Fixed public representations

The C API uses only these primitive representations:

| Meaning | Representation |
|---|---|
| Signed integers | `int8_t`, `int16_t`, `int32_t`, `int64_t` |
| Unsigned integers | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` |
| Boolean | `CNA_Bool` = `uint8_t`, with only `CNA_FALSE` (0) and `CNA_TRUE` (1) valid — see below |
| Result/category/flag/set identifiers | typedef of `uint32_t` plus named integer constants, never a C `enum` field |
| Length/count/capacity/offset | `uint64_t` |
| Floating point | IEEE-754 binary32 `float` or binary64 `double`, pinned by the C and C++ header assertion walls |
| Handle | `CNA_Handle` = `uint64_t` |

`long`, `unsigned long`, `wchar_t`, `size_t`, raw C++ `bool`, a compiler-selected enum underlying
type and bit-fields are prohibited in ABI parameters, ABI fields and callback signatures. Native
adapters must validate narrowing conversion to host `size_t` or CNA integer aliases and return
`CNA_RESULT_OVERFLOW` on failure.

## POD structs

All public structs are standard C POD-like layouts made only from the representations above and
pointers to such values. Extensible input/output structs begin with:

```text
uint32_t struct_size;
uint32_t struct_version;
```

The caller initializes `struct_size` to `sizeof(the exact known struct)` and `struct_version` to the
documented version. Every current versioned structure uses version `1`. CNA requires the exact
version understood by that route and at least the complete size compiled into the library. It
validates the known fields required by the route, including reserved storage where defined; a
larger same-version structure is accepted and its unknown tail is ignored, while a smaller
structure returns `CNA_RESULT_INVALID_ARGUMENT`.

This is one-way forward tolerance: a newer, larger caller can talk to an older library, but an older,
smaller caller cannot satisfy a newer library that has enlarged the same structure. Appending a field
to an existing structure is therefore an ABI break, not a routine additive change. Compatible growth
uses existing reserved storage or introduces a new explicitly supported structure version, structure
type or route while preserving the old contract.

The generated C baseline records `sizeof`, alignment and every field offset and size for every
public ABI struct. The hand-written C and C++ assertion walls additionally pin selected high-risk
layouts. Public structs may not rely on packing pragmas. The API uses explicit fields rather than
native compiler bit-field layout. A C API value struct is converted field-by-field to its C++
counterpart; it is never `reinterpret_cast` to an XNA/Sharp Runtime value type.

## API evolution

New capabilities use one of these paths:

1. Add a new function with a new name.
2. Use reserved storage, or add a new explicitly versioned structure or route while keeping the old
   contract available.
3. Add a new named `uint32_t` constant after checking numeric stability.
4. Add an explicitly labelled experimental header/function family before ABI 1.0.

During experimental `0.x`, changing an existing meaning is an incompatible change governed by the
reviewed minor-version rule above. From `1.x`, it is forbidden within an ABI major. Deprecated
functions continue to export through their ABI major and direct consumers to their replacement in
documentation. The C API's version is independent of the CNA project version and independent of
Sharp Runtime's version.

## The recorded baseline

The rule above is only worth what enforces it, and a rule about numbers nobody reads needs
something that reads them. `tools/c-api/abi_baseline.json` is a checked-in snapshot of what the ABI
**actually is**, produced by `tools/c-api/generate_abi_baseline.py`:

| Recorded | Measured how |
|---|---|
| Every struct's size and alignment, and every field's offset and size | `sizeof`, `_Alignof` and `offsetof` in a generated probe |
| Every scalar typedef's width and alignment | the same probe |
| Every integer, floating-point, string and named-color constant's value, flag bits written `(UINT32_C(1) << n)` and the masks built from them included | printed by the probe, so a macro's *expansion* is what is recorded; `_Generic` decides integer or floating |
| The ABI version the headers declare, and the one the library reports | `CNA_ABI_VERSION` against `cna_get_abi_version()` |
| Every `cna_*` symbol the shared object exports | `nm -D --defined-only` |

The point is not that these numbers are asserted — the assertion walls in
`tests/pure_c/AbiHeaderC.c` and `tests/cpp/AbiHeaderCpp.cpp` already pin the ones each slice
remembered to pin — but that a change to **any** of them arrives as a reviewable diff instead of a
silently different binary.

When the check disagrees with the build it says which kind of difference it found:

- **Additions** — a new struct, field, constant or export. The label describes the generated diff;
  it does not by itself declare the change compatible. In particular, adding a field to an existing
  structure also changes its recorded layout and is an ABI break. Re-record compatible additions
  with `--write`, and the diff shows exactly what was added.
- **ABI breaks** — a struct that changed size or alignment, a field that moved or was resized, a
  constant whose value changed, an export that vanished, or a library whose reported version
  disagrees with its headers. Each is named individually. These are what this section forbids.

Two gates run it:

- `CApiAbiHeaderBaseline` measures the headers alone. It runs in native, non-cross-compiling C API
  build trees and in the Linux `c-api-abi-baseline` CI job, which is where a moved field is most
  likely to be introduced.
- `CApiAbiBaseline` adds the two halves only a built library can answer — its own ABI version and
  its export list — and runs for native ELF and Mach-O C API libraries.

The probe measures parenthesized macros (flag bits and masks such as
`CNA_RENDERER_FORMAT_USAGE_ALL`) and floating constants at full precision (CBIND-159);
`tools/c-api/test_generate_abi_baseline.py` (`CApiAbiBaselineTests`) proves that a moved or removed
flag and a changed mask are reported as breaks, and that no value macro of the public headers goes
unmeasured.

What one cannot see, it skips **by name**: a header-only run reports the export list and the
runtime version as skipped rather than passing over them in silence.

Recording the baseline needs the library:

```sh
python3 tools/c-api/generate_abi_baseline.py --write --library <build>/modules/c-api/libcna_c_api.so
```

The current baseline records 3,397 exports; build configurations must export the same surface.
That is itself part of the contract:
the ABI **surface** does not vary with the renderer, with `CNA_DEVICES`, or with `CNA_CNAEXT` —
only the answers do. A route whose renderer facility or optional feature is absent exists and
refuses, rather than disappearing from the library. `CNA_CNAEXT` controls the ASCII, CRT,
colour-depth and DebugDraw extension families; `CNA_DEVICES` controls optional device and sensor
support. Their routes remain declared and exported when the corresponding implementation is absent
and answer `CNA_RESULT_NOT_SUPPORTED` where appropriate.
