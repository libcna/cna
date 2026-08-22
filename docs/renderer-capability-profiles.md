# Detailed renderer capability profiles

Status: implemented as `MOD-2203` on 2026-08-22.

CNA has two capability-discovery levels. `GraphicsCapability` remains the small compatibility
summary used by existing code. `RendererCapabilityProfile` is the detailed, immutable snapshot for
tools, diagnostics, renderer selection and code that must distinguish closely related promises.
It is a CNA extension, not part of XNA 4.0.

## Why the catalog starts at 30 rather than an arbitrary large number

The detailed enum is append-only, but an entry is added only when CNA can define an observable
contract and classify it from an audited renderer query. The initial catalog contains 30 atomic
features: the 19 legacy capabilities, separated source acceptance/execution, compute-image
binding, shadows, image-based lighting, GPU timers and six shader dialects. Reserving or publishing
hundreds of guessed identities would turn "unsupported" into a mixture of absent, unaudited and
not-yet-defined behavior.

Every current feature is classified as one of:

- `Unknown`: not audited or probed; callers must not interpret this as rejection;
- `Unsupported`: the complete contract is unavailable;
- `Supported`: the complete documented contract is available;
- `Restricted`: only the subset named by the accompanying English note is available.

The current profile builder maps every initial feature explicitly, so a live profile has no
`Unknown` feature answers. New enum entries default to `Unknown` until their mapping is added.

## Three independent sections

`GraphicsDevice::GetRendererCapabilityProfileEXT()` returns one cached snapshot containing:

1. atomic feature classifications and optional qualifications;
2. numeric limits with a separate `known` flag;
3. per-`SurfaceFormat` `knownUsages` and `supportedUsages` masks.

The two format masks are intentional. For example, a missing `Sampled` bit in `knownUsages` means
that this support path has not been classified; it does not mean the format cannot be sampled.
`supportedUsages` is always a subset of `knownUsages`.

The initial limit section consolidates the existing texture, vertex-stream, compute and
vertex-storage-block queries. The initial format section classifies the three facts the existing
renderer boundary can answer honestly: texture storage, render-target creation and `Color`-shaped
transfer. The remaining usage identities already have stable names but stay unknown. Completing
the broader native limit and format probes remains tracked by `MOD-2220` and `MOD-2221`; this
profile does not mark those tasks complete prematurely.

## English limitations and complete report

`IGraphicsRenderer::GetAdditionalLimitationsTextEXT()` lets an implementation append stable
English qualifications that do not fit a structured identity. The common profile also supplies
cross-renderer caveats, including feature-combination, device-reconstruction, emulation,
performance and driver-evidence boundaries.

`GraphicsDevice::GetRendererCapabilityReportEXT()` generates one long English report from the
same cached structured facts. It lists all features, all limits, all 27 current surface formats and
the additional limitations. The text is diagnostic output, not a machine protocol: applications
must branch on the enum, value and masks, never parse report sentences.

The snapshot is invalidated and rebuilt when the native renderer is reconstructed or an applied
presentation change can affect capabilities.

## C ABI

The C ABI deliberately does not enlarge `CNA_GraphicsCapabilityFlags`, which is a published
64-bit value. It exposes a separate append-only identity space and individual queries:

- `cna_graphics_device_get_renderer_feature_support_ext`;
- `cna_graphics_device_get_renderer_limit_ext`;
- `cna_graphics_device_get_surface_format_support_ext`;
- `cna_graphics_device_get_capability_report_size_ext` and
  `cna_graphics_device_copy_capability_report_ext`.

The report follows the existing two-call byte-buffer convention. Its size excludes a terminator,
copying is all-or-nothing, and the function does not append a terminator.

## Extension rules

- Append feature and limit identities; never reorder or reuse a numeric value.
- Define a feature as one observable contract. Split two promises when a renderer can truthfully
  answer them differently.
- Default new renderer queries to false or unknown, never permissive success.
- Add structured fields before prose when the fact is enumerable or numeric.
- Keep prose in English and stable enough for diagnostics, but do not make it parseable API.
- Add C/C++ ordinal tests and map every live renderer answer before changing `Unknown` to another
  state.
