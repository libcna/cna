# Detailed renderer capability profiles

Status: reconciled with the C++ API, profile builder, renderer boundary, C ABI and executable
contract tests on **2026-11-01**.

`CNA::RendererCapabilityProfile` is CNA's detailed, read-only snapshot of the active
`GraphicsDevice`. It is a CNA extension, not XNA 4.0 API. Use it when the older
`CNA::GraphicsCapability` summary is too broad: the profile separates atomic feature contracts,
numeric limits and per-`SurfaceFormat` usages.

The answer belongs to the selected device, graphics profile, native adapter/driver and current
presentation state. A renderer name or the project-wide
[`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) is useful orientation,
but neither is a substitute for querying the live device.

## Query layers

`GraphicsDevice` exposes two related layers:

- `SupportsCapability(CNA::GraphicsCapability)` is the compact compatibility query used by
  existing CNA code. In particular, `MultipleRenderTargets` and `Texture3D` also respect the
  device's `GraphicsProfile`.
- `GetRendererCapabilityProfileEXT()` returns the cached detailed snapshot. Convenience methods
  query the same snapshot: `GetRendererFeatureSupportEXT()`,
  `SupportsRendererFeatureEXT()`, `GetRendererLimitEXT()`,
  `GetRendererSurfaceFormatSupportEXT()` and `GetRendererCapabilityReportEXT()`.

The detailed catalog deliberately splits promises that a renderer may answer differently. For
example, accepting a `ShaderEffect` object is separate from executing its supplied source;
`Texture3D` storage/transfer is separate from shader sampling; and the six source dialects are
reported independently.

## Feature catalog and support states

`CNA::AllRendererFeatures()` is the canonical enumerable list. The current catalog contains 33
defined identities:

| Ordinals | Contracts |
|---|---|
| 0-17 | the 17 legacy capability mappings plus the separate `ShaderEffectSourceExecution` contract |
| 21-22 | `ShadowSampling`, `ImageBasedLighting` |
| 24-29 | desktop GLSL, GLSL ES, Vulkan GLSL, HLSL, MSL and WGSL source dialects |
| 30-36 | `Texture3DSampling`, `BaseInstanceDrawing`, sampler LOD bias, PBR and skinned-PBR shading, `MultiSampleMask`, render-target readback |

Ordinals 18, 19, 20 and 23 belonged to the retired compute/indirect/timer layer. They remain
reserved and are not returned by `AllRendererFeatures()`. Existing ordinals are append-only and
must never be reordered or reused.

Every feature has one `RendererFeatureSupport` value:

| State | Meaning |
|---|---|
| `Unknown` | The contract has not been classified. Do not interpret it as support or rejection. |
| `Unsupported` | The complete contract is unavailable; its feature note defines refusal, no effect or fallback. |
| `Supported` | The complete documented contract is available. |
| `Restricted` | Only the explicitly documented subset is available. Read the feature note before use. |

`SupportsRendererFeatureEXT()` and `RendererCapabilityProfile::Supports()` return `true` only for
`Supported`; `Restricted` deliberately returns `false`. The current live-device builder assigns
every defined feature explicitly, so a built profile has no `Unknown` feature entries. A default
profile and invalid or reserved identities remain `Unknown`.

Some contracts need feature-specific interpretation:

- unsupported `SamplerLevelOfDetailBias` is accepted but has no sampling effect;
- unsupported `PbrEffectShading` or `SkinnedPbrEffectShading` uses the documented untextured,
  unlit fallback;
- `MultiSampleMask` may be `Restricted`: SDLGPU applies all-samples and no-samples masks and
  refuses other partial masks;
- unsupported `RenderTargetReadback` makes `Texture2D::GetData` on a render target throw
  `System::NotSupportedException`.

These are observable contracts, not maturity labels or performance estimates. A silent wrong
result on a claimed supported path is a defect; it is not a capability restriction.

## Numeric limits

`CNA::AllRendererLimits()` enumerates the eight defined limits:

- `MaxTextureDimension` and `MaxVertexStreams`;
- `MaxUniformBufferBytes` and `MaxSampledTexturesPerShaderStage`;
- `MaxVertexInputBindings` and `MaxVertexInputAttributes`;
- `MaxColorAttachments` and `MinUniformBufferOffsetAlignment`.

Each query returns `RendererLimitValue { known, value }`. Consumers must inspect `known` before
using `value`; an unknown query returns `{false, 0}`. Zero in a known result is a real classified
value and commonly means that the corresponding facility is unavailable. All eight defined limits
are classified by the current live-device builder.

Limit ordinals 2-10, 12, 13, 15, 19 and 21 are retired and permanently reserved. Ordinal 21 lies
at the former end of the range rather than below the current C++ `Count`. The canonical enumerable
list therefore matters more than walking every integer below `Count`.

## Surface-format usage masks

For each of the 27 current `SurfaceFormat` values, `RendererFormatSupport` carries two masks:

- `knownUsages` says which usage questions have a classified answer;
- `supportedUsages` says which classified usages are supported and is always a subset of
  `knownUsages`.

A missing known bit means **unknown**, not unsupported. `IsKnown()` tests the first condition;
`Supports()` is true only when the requested bits are both known and supported.

The defined usage bits are `TextureStorage`, `Sampled`, `Filterable`, `RenderTarget`, `Blendable`,
`TransferSource`, `TransferDestination`, `Mipmapped`, `Multisample` and `ColorTransfer`. Bits 5-7
belonged to retired storage-image usages and remain reserved.

The common profile builder always classifies `TextureStorage`, `RenderTarget` and `ColorTransfer`
for every current format through the same framework verdicts used by resource construction and
transfer. A renderer may add audited facts through `GetSurfaceFormatUsageSupportEXT()`; for bits it
marks known, its answer overrides the common verdict. Every other omitted bit remains unknown.
Native API availability alone is not enough: the implemented CNA path must also satisfy the usage
contract.

Adding a `SurfaceFormat` requires extending the profile's format/name catalog. A compile-time check
currently pins the contiguous range from `Color` (0) through `UShortEXT` (26).

## Snapshot lifetime and report

The detailed profile is built lazily on the first query and cached by `GraphicsDevice`. Repeated
queries return the same const object until CNA invalidates it. Invalidation occurs when:

- a device reset applies presentation formats or a possibly clamped MSAA count;
- `SetGraphicsProfileEXT()` changes Reach/HiDef policy;
- the native renderer is created, reconstructed or destroyed.

The next query builds a fresh snapshot. A caller that must retain the old observation across one
of those operations should copy the value rather than retain the device-owned reference.

`IGraphicsRenderer::GetAdditionalLimitationsTextEXT()` supplies stable qualitative notes that do
not fit a structured field. The generated English report contains profile schema 3, renderer name,
all defined features, all defined limits, all 27 formats and the combined limitations text.
`GetRendererCapabilityReportEXT()` returns that cached report.

The report is diagnostic text, not a machine protocol. Applications must branch on the structured
feature, limit and format queries and must not parse report wording, ordering or punctuation.

## C ABI

The C ABI keeps the published `CNA_GraphicsCapabilityFlags` summary unchanged and exposes the
detailed model through individual calls on a callback-scoped borrowed graphics-device handle:

- `cna_graphics_device_get_renderer_feature_support_ext`;
- `cna_graphics_device_get_renderer_limit_ext`;
- `cna_graphics_device_get_surface_format_support_ext`;
- `cna_graphics_device_get_capability_report_size_ext` and
  `cna_graphics_device_copy_capability_report_ext`.

The C feature and limit constants use the same append-only ordinals as C++. Reserved or
out-of-range feature/limit identities return `CNA_RESULT_INVALID_ARGUMENT`; they do not return an
`Unknown` value. An unavailable but defined feature is a successful query returning
`CNA_RENDERER_FEATURE_SUPPORT_UNSUPPORTED`. Invalid surface-format values and null required output
pointers are also invalid arguments.

The report uses CNA's two-call UTF-8 byte-buffer convention. The size excludes a terminator. Copy
is all-or-nothing, writes no terminator and reports the required size with
`CNA_RESULT_BUFFER_TOO_SMALL` when capacity is insufficient.

The complete C-facing renderer-selection and borrowed-handle contract is in
[`c-api/RENDERERS_AND_CAPABILITIES.md`](c-api/RENDERERS_AND_CAPABILITIES.md).

## Extension and verification rules

- Add only an observable contract that the renderer boundary can classify. Split promises that a
  renderer can truthfully answer differently.
- Append feature, limit and usage identities; never reorder or reuse published values.
- Prefer a structured feature, number or mask over prose when callers can safely branch on it.
- Default new renderer hooks conservatively and map every defined identity in the profile builder.
- Keep the C constants, mapping functions and ordinal/static assertions synchronized with C++.
- Extend `RendererCapabilityProfileTest` for catalog, snapshot, limit, mask and report behavior;
  extend the pure-C `GraphicsDeviceSmoke` for C ABI ranges and buffer semantics.
- For a rendering claim, add a real-renderer observation. `CrossRendererContractTest` currently
  compares reported LOD bias, PBR/skinned-PBR shading, multisample-mask and render-target-readback
  answers with observed output or deterministic refusal in multi-renderer builds.

Current defects belong only in [`handoff.md` section 5](../handoff.md#5-known-bugs-and-limitations);
finished implementation history remains in Git.
