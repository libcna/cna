# CNA C API Renderers and Capabilities

## CNA owns renderer selection

The C API uses the renderer selected by CNA's build configuration. It does not expose a renderer
pointer or create a separate runtime renderer-selection path. The C API reports the active renderer
identity, build/platform capabilities and supported feature flags using fixed-width POD output.

An operation unavailable on the active renderer returns `CNA_RESULT_NOT_SUPPORTED` and leaves any
documented output parameter unchanged. It must not silently substitute different graphics behavior.

## Capability queries

`cna_game_get_graphics_device` returns one callback-scoped borrowed device handle. Repeated queries
within the same callback return the same handle; CNA invalidates it before the callback returns.
The handle cannot be obtained outside a lifecycle callback and has no release operation.

`CNA_RendererInfo` reports a stable `CNA_GraphicsRendererType`, the canonical renderer name length,
the device's maximum single-axis texture dimension and a capability bit set. The matching
one-capability query is authoritative when new capabilities no longer fit the current structure
version. Renderer names use the UTF-8
query/copy protocol rather than raw native string pointers. Every answer delegates to
`GraphicsDevice::GetGraphicsRendererName`, `GetMaxTextureDimension` or `SupportsCapability`; the C
adapter does not maintain a renderer-name feature table that could drift from CNA.

A recognized capability that is unavailable is a successful query returning `CNA_FALSE` and an
unset bit. An operation that requires it returns `CNA_RESULT_NOT_SUPPORTED` and leaves documented
outputs unchanged. An unknown capability identifier returns `CNA_RESULT_INVALID_ARGUMENT`.

Platform-device availability uses the same successful-snapshot rule. `cna_touch_get_capabilities`
reports connection and maximum touches, while `cna_audio_get_capabilities` probes CNA's real native
audio mixer and reports whether playback can currently be opened. Applications therefore need not
infer device support from the graphics renderer, operating-system name or build configuration.
The audio probe can initialize CNA's process-wide mixer but creates no owned C handle.

## Initial SpriteBatch state boundary

The initial batched C path maps every native `SpriteSortMode` and the two `SpriteEffects` bits. Its
begin state is intentionally fixed to XNA's defaults: AlphaBlend, LinearClamp,
DepthStencilState.None, CullCounterClockwise, identity transform and no custom effect. State-object
and effect handles are later coverage work; the adapter does not invent approximate replacements.
If a renderer raises `System::NotSupportedException` while creating, beginning, submitting or
ending this path, the exception barrier returns `CNA_RESULT_NOT_SUPPORTED`. The active-batch
destroy route remains available for cleanup after a failed flush.

## Backbuffer readback boundary

`CNA_BackBufferInfo` reports the logical dimensions and format from canonical presentation
parameters. Full-buffer RGBA8 readback uses the native `GraphicsDevice::GetBackBufferData(Color*)`
route after capacity validation and copies through native `Color` values; it never exposes or
reinterprets renderer memory. HEADLESS has no rasterized backbuffer and returns
`CNA_RESULT_NOT_SUPPORTED` without modifying the destination. The SDL_RENDERER C test verifies the
exact red, green and blue uploaded texels plus an untouched clear pixel before presentation.

## Test policy

HEADLESS is the deterministic lifecycle/state control. It proves C callback order, handle lifetime,
error behavior and honest refusal when no pixel result exists. It cannot by itself prove visual
pixel correctness. The initial 2D slice therefore runs the same C test on SDL_RENDERER and checks
exact pixels. Every later rendering claim requires an appropriate real-renderer observation in
addition to HEADLESS. Renderer-specific unavailability is recorded in the public C API coverage
matrix and feature documentation.
