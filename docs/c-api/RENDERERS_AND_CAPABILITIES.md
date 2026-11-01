# CNA C API renderers and capabilities

Status: reconciled with the C headers, C++ adapters, detailed capability profile and pure-C
contract tests on **2026-11-01**.

## Renderer ownership and selection

The C API uses CNA's renderer registry and `GraphicsRendererSelection`; it does not expose a
renderer pointer or maintain a second selection mechanism. A single-renderer build contains one
identity. A multi-renderer build contains several and may select or fall back at run time as
documented in [`runtime-renderer-selection.md`](../runtime-renderer-selection.md).

The C surface distinguishes four renderer answers that must not be conflated. The first three are
no-device queries in `core_ext.h`; the fourth belongs to a borrowed graphics device:

- `cna_graphics_renderer_get_current_type` and its name/category/maturity companions describe the
  build's **compile-time default**;
- `cna_graphics_renderer_get_selected_ext` reports the renderer CNA will try first;
- `cna_graphics_renderer_get_active_ext` reports the renderer that actually created the device and
  refuses the query before selection is latched;
- `cna_graphics_device_get_renderer_info` reports the active identity of that borrowed device.

The `_available_*_ext` routes enumerate the identities compiled into the binary. Preferred
identity, explicit fallback chain and automatic fallback may be changed only before the first
graphics device latches selection. The fallback-history routes then expose every rejected attempt,
its stable reason and its diagnostic text. Category and maturity are classifications, not feature
promises; their exact mapping is in [`renderer-registry.md`](../renderer-registry.md).

## Borrowed graphics-device handle

`cna_game_get_graphics_device` returns a borrowed handle in either of two validity windows:

- during a lifecycle callback, from the callback-borrowed game handle, until that callback returns;
- before the first run, step or tick, from the owned game handle, until the first callback returns.

Repeated queries in the same window return the same handle. It has no release operation and must
not be retained past its window. Once a run has begun, an out-of-callback query is refused. Owned
resources created through the borrowed device, such as textures and a `SpriteBatch`, have their own
handles and lifetimes; the borrowed device itself never becomes owned by C.

## Capability query layers

The answers belong to the active device, its graphics profile, native adapter/driver and current
presentation state. Renderer identity is useful orientation but is not a safe substitute for a
capability query.

### Legacy summary

Version 1 of `CNA_RendererInfo` reports:

- the active `CNA_GraphicsRendererType`;
- canonical renderer-name byte length;
- maximum two-dimensional texture width or height;
- the 64-bit `CNA_GraphicsCapabilityFlags` compatibility summary.

The name itself uses the normal UTF-8 size/copy pair. The adapter obtains every value from the
native `GraphicsDevice`; it has no independent renderer-name capability table.

`cna_graphics_device_supports_capability` is the authoritative one-capability form. The defined
`CNA_GraphicsCapability` identities are contiguous from `THREE_D` through `MAXIMUM`. A recognized
but unavailable capability succeeds and writes `CNA_FALSE`; an undefined identity returns
`CNA_RESULT_INVALID_ARGUMENT`. `MULTIPLE_RENDER_TARGETS` and `TEXTURE_3D` also reflect the current
Reach/HiDef graphics profile.

The flags remain a compatibility summary. New code that needs an atomic promise, a numeric limit
or a per-format answer should use the detailed layer rather than infer it from a broad bit.

### Detailed profile

The CNAEXT routes expose the same cached `RendererCapabilityProfile` described in
[`renderer-capability-profiles.md`](../renderer-capability-profiles.md):

- `cna_graphics_device_get_renderer_feature_support_ext` returns `UNKNOWN`, `UNSUPPORTED`,
  `SUPPORTED` or `RESTRICTED` for one defined feature;
- `cna_graphics_device_get_renderer_limit_ext` returns a `known` flag and a value, with zero used
  for an unknown value and also permitted as a real known limit;
- `cna_graphics_device_get_surface_format_support_ext` returns separate known-usage and
  supported-usage masks, with supported bits always a subset of known bits;
- the capability-report size/copy pair exposes the cached diagnostic report as non-terminated
  UTF-8 text.

`RESTRICTED` is not equivalent to `SUPPORTED`; callers must read the feature-specific contract.
An absent known-usage bit means unknown, not unsupported. The report is diagnostic prose and must
not be parsed as a machine protocol.

Feature and limit identities are append-only but sparse because retired values stay reserved.
Reserved or out-of-range feature/limit values return `CNA_RESULT_INVALID_ARGUMENT`, not an
`UNKNOWN` answer. Format-usage bits 5-7 are likewise retired, and
`CNA_RENDERER_FORMAT_USAGE_ALL` excludes them. The canonical defined catalogs, retired gaps,
snapshot invalidation points and operation-specific meanings belong to the detailed-profile page.

## Renderer identity values

`CNA_GraphicsRendererType` is a stable, sparse numeric identity. Live constants are never
renumbered and a retired value is never reused. `CNA_GRAPHICS_RENDERER_UNKNOWN` is report-only and
is refused by every identity-taking route.

`CNA_GRAPHICS_RENDERER_MAXIMUM` is the highest-valued **live** identity (`FNA3D`, value 43). It is
not a count, a promise that every lower value is live or the highest value ever assigned: retired
values also exist above it. A consumer that needs this binary's usable set must call the
`_available_*_ext` routes rather than scan integers and assume each one is valid. The permanent
retired-name/value ledger and next never-assigned value are in
[`removed-renderers.md`](../removed-renderers.md).

A retired numeric value returns `CNA_RESULT_INVALID_ARGUMENT`; the error message identifies the
retired renderer and says that its value remains reserved. The name-taking selection route gives
the corresponding retired-name diagnostic. The predicate
`cna_graphics_renderer_try_parse_name_ext`, by contrast, succeeds with `out_recognized` false for
both unknown and retired names because neither names a public live identity. `SDL_GPU` is a
refused former CMake spelling of live `SDLGPU`, not another retired value; the C name parser simply
does not recognize it.

The C/C++ identity mapping and live/retired registries are mechanically checked by
`scripts/check_renderer_identities.py`. Adding, removing or restoring a renderer requires an
explicit C identity decision; an unmapped renderer fails the registry gate instead of degrading
to `UNKNOWN` at run time.

## SpriteBatch state boundary

`cna_sprite_batch_begin` maps every native `SpriteSortMode` and uses XNA's default state tuple:
AlphaBlend, LinearClamp, DepthStencilState.None, CullCounterClockwise, identity transform and the
default sprite effect. The two `SpriteEffects` mirror bits are accepted per submitted command.

The wider overloads are also exposed:

- `cna_sprite_batch_begin_with_states` accepts complete blend, sampler, depth/stencil and
  rasterizer descriptors;
- `cna_sprite_batch_begin_with_effect` accepts optional state descriptors, an owned Effect handle
  from the same game and an optional transform. `CNA_INVALID_HANDLE` selects the default effect and
  a null matrix selects identity.

If the active renderer refuses creation, begin, submission or flush with
`System::NotSupportedException`, the exception barrier returns `CNA_RESULT_NOT_SUPPORTED`.
Destroying an active batch cancels queued deferred commands and remains available for cleanup
after a failed end/flush.

## Backbuffer readback boundary

`CNA_BackBufferInfo` reports logical dimensions and the canonical presentation format.
`cna_graphics_device_get_backbuffer_data_rgba8` reads the complete logical backbuffer and reports
the required pixel count. `cna_graphics_device_get_backbuffer_data_window` accepts a versioned
source rectangle, destination start index and element count for a bounded readback. Both copy
through native `Color` values; neither exposes or reinterprets renderer memory.

Capacity and structure errors are decided before native readback. Pixel writes are all-or-nothing,
including when the renderer returns `CNA_RESULT_NOT_SUPPORTED`. Callers must still follow each
route's documented scalar-output rules: the full-buffer route reports its required count before
the native read is attempted. `HEADLESS` has no rasterized backbuffer and honestly returns
`CNA_RESULT_NOT_SUPPORTED`.

## Verification policy

The pure-C suites divide the contract by responsibility:

- `CoreExtSmoke` covers identities, compile-time/selected/active answers, availability, fallback
  and retired diagnostics;
- `GraphicsDeviceSmoke` covers the detailed feature/limit/format ranges, report buffers and
  windowed backbuffer readback;
- `GraphicsSurfaceSmoke` covers default, explicit-state and effect-bearing `SpriteBatch` begin;
- `LifecycleSmoke` covers borrowed-handle lifetime, the legacy summary and exact full-backbuffer
  pixels where readback exists.

`HEADLESS` is the deterministic lifecycle, state and honest-refusal control; it cannot prove pixel
correctness. Rendering claims therefore require a real-renderer observation in addition to the
HEADLESS route. Current C API coverage is in [`COVERAGE.md`](COVERAGE.md), and current defects and
platform qualification gaps belong only in
[`handoff.md` section 5](../../handoff.md#5-known-bugs-and-limitations).
