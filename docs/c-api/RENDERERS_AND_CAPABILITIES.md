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

## Test policy

HEADLESS is the deterministic lifecycle/state control. It can prove C callback order, handle
lifetime, error behavior and native command/state observations. It cannot by itself prove visual
pixel correctness. Any C API rendering feature that claims visual output requires an appropriate
real renderer test in addition to its HEADLESS test. Renderer-specific unavailability is recorded
in the public C API coverage matrix and feature documentation.
