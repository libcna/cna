# C Graphics Resources, States, Display Values, Render Targets and Sprite Fonts

## Scope

This page covers the shared resource, state, display, render-target and font boundary. The public
headers are all included by `CNA/C/cna.h`:

| Header | Contract |
|---|---|
| `graphics_resource.h` | Shared names, tags, disposal, device queries and disposing subscriptions. |
| `graphics_state.h` | Copied blend/depth/rasterizer/sampler descriptors and SpriteBatch state/effect begin. |
| `display.h` | Display modes, adapter enumeration, format negotiation and presentation parameters. |
| `render_target.h` | Owned targets, subresource bindings, applied properties and content-loss events. |
| `sprite_font.h` | Owned fonts, copied glyph tables, texture retention and measurement. |

The Texture/Texture2D construction, typed-transfer and encoded-image contract is
documented separately in [`TEXTURES.md`](TEXTURES.md).
The Texture3D/TextureCube handle and region-transfer contract is documented in
[`TEXTURE_VOLUMES.md`](TEXTURE_VOLUMES.md).
Effect resources and their transitive descendant-view lifetime are documented in
[`EFFECTS.md`](EFFECTS.md).
Device ownership, frame control and capability queries are in
[`GRAPHICS_DEVICE.md`](GRAPHICS_DEVICE.md) and
[`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md).

All extensible inputs and outputs begin with `struct_size` and `struct_version`. Version one
callers initialize both fields before a query; preset/value initializers fill the complete output.
Fixed-width identities and layouts are frozen by both C17 and C++23 compile-time assertions.

## Common graphics-resource contract

Texture2D, Texture3D, TextureCube, RenderTarget2D, RenderTargetCube, VertexDeclaration, VertexBuffer,
IndexBuffer, Effect and OcclusionQuery handles support the generic `cna_graphics_resource_*`
operations. The resolver checks handle kind, generation and thread ownership before reading the
native resource. SpriteFont, SpriteBatch, ASCII and DebugDraw handles do not become generic
graphics resources merely because they use a device.

### Device identity

`cna_graphics_resource_get_graphics_device` returns `CNA_RESULT_SUCCESS` with
`CNA_INVALID_HANDLE` when the native resource has no device, as with a standalone declaration.
For a game-owned resource it uses the game's existing borrowing window: during a callback, or
before the first run until the first callback returns. The resource outlives that borrowed handle.
The getter does not transfer device ownership.

For a resource created on an independently owned device, the getter returns that exact existing
device handle. It verifies the native device identity and does not acquire another ownership
reference or convert the device into a game loan. The caller must keep the device alive while
using its resources. `CApi_OwnedGraphicsDeviceSmoke` verifies two distinct resource/device pairs,
queries the surviving pair after the other device is destroyed, and rejects a destroyed resource
without publishing a device handle. The regression fails before CNA-FIX-D17 and passes after it.

### Names and tags

Names are validated length-delimited UTF-8. Count/copy operations return exact bytes without an
implicit terminator and never write a partial result. `ToString` returns the name when nonempty and
the native fully qualified type name otherwise. The C tag is a fixed 64-bit opaque token stored
per handle in the registry, with zero as its default. It does not expose or mutate native
`System::Object* Tag`. Different C aliases of the same native resource have separate C tags, and
releasing/reusing a registry slot resets its token.

### Disposal and retention

`cna_graphics_resource_dispose` keeps the C handle alive so callers may query `is_disposed` and
then release it through its typed destroy route. When disposal is permitted, repeated native
disposal is a successful no-op. The generic route returns `CNA_RESULT_INVALID_STATE` for a
non-disposable borrowed view or a resource retained by an active C API owner. This includes
Texture2D resources held by a SpriteBatch or font, textures held by effects, resources held by
skinned-model bundles and render targets held by a render-target scope. Native disposal also
rejects a target that is still bound. Generic disposal cannot bypass those lifetime guards.

A disposing subscription owns a separate registration handle and invokes its caller-owned
function/context synchronously after native `IsDisposed` becomes true. Effect and OcclusionQuery
handles accept a subscription that is never invoked: XNA's Effect and OcclusionQuery set their
disposed flag before the shared GraphicsResource disposal, which therefore never raises their
`Disposing` event. It does not keep the source alive or block source destruction. The
registration remains releasable after the resource is gone;
a second unregister returns `CNA_RESULT_INVALID_HANDLE`. These routes retain owner-thread checks;
the opt-in foreign-thread service and callback rules are defined in
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

## Graphics state values

`CNA_BlendState`, `CNA_DepthStencilState`, `CNA_RasterizerState` and `CNA_SamplerState` are copied
POD descriptors. Their `cna_*_state_init` functions materialize the native default and named XNA
presets. A caller may then change fields and use the matching graphics-device get/set operation.
Sampler operations select a stage with `CNA_ShaderStage`. Pixel samplers expose 16 slots; vertex
samplers expose four in HiDef and none in Reach. Unknown stages and indices at or above the
16-slot storage maximum return `CNA_RESULT_INVALID_ARGUMENT`; a vertex index within storage but
outside the profile's active range returns `CNA_RESULT_INVALID_ARGUMENT` with
`CNA_ERROR_CATEGORY_RANGE`.

Preset initialization is renderer-independent and may run on any thread. Applying/querying a
device state requires a valid owned or borrowed device handle under the normal owner-thread
rules. Descriptor validation checks versions, defined enum values, canonical booleans, reserved
bytes and applicable numeric fields before applying state. Native renderer limitations pass
through the exception barrier. The detailed state behavior is maintained in the native
blend/depth/rasterizer/sampler documentation.

`cna_sprite_batch_begin_with_states` accepts explicit state descriptors.
`cna_sprite_batch_begin_with_effect` additionally accepts an optional effect and transform;
`CNA_INVALID_HANDLE` selects the stock effect and a null transform selects identity. The full
SpriteBatch boundary is in
[`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md#spritebatch-state-boundary).

The native C++ `SpriteBatch::Begin` overloads take `const BlendState&` and retain its shared
identity. Deferred batches observe source mutations and disposal at `End`; Immediate binds the
original state at `Begin`. The C routes materialize their own native states from the copied POD
descriptors before returning, so the caller's descriptor storage need not survive the call.
The [reference provenance](../../tests/reference/xna40/spritebatch-blend-reference/PROVENANCE.json)
records genuine XNA observations for all four non-null overloads. This C++ signature correction
requires native C++ consumers to rebuild; the C ABI signatures are unchanged (CNA-FIX-D23).

## Display, adapters and presentation

`CNA_DisplayMode` and `CNA_PresentationParameters` are value snapshots. Display modes support
initialization and equality. Adapter collections use count/copy APIs, optional SurfaceFormat
filtering and index-based metadata/string/profile/format queries. Adapter enumeration accepts an
owned game handle even before its first callback, or a valid device handle; the adapter index is
a point-in-time enumeration index. Format negotiation reports both the selected color/depth/MSAA
values and whether they exactly match the request. Description and device-name copies contain
exactly the reported UTF-8 byte count and no implicit terminator.

Presentation initialization uses XNA's native defaults: a 0×0 backbuffer, fullscreen true, Color
format, no depth/stencil attachment and no MSAA. Set dimensions and windowed mode explicitly when
creating an independent windowed device. Clone executes native `Clone`, rather than copying C
bytes. Setting presentation parameters preserves the device's existing native window; use the
device reset or manager apply-changes routes for their respective reconfiguration workflows.

The two legacy routes in `display.h`, `cna_graphics_adapter_get_native_monitor_handle` and
`cna_graphics_device_get_native_window_handle`, zero their scalar output and return
`CNA_RESULT_NOT_SUPPORTED` after resolving a valid context. Native interop is separately available
through `cna_game_window_get_native_handle_ext` and `cna_game_window_get_native_window_ext` in
`runtime_window.h`; see [`GAME_COMPONENTS.md`](GAME_COMPONENTS.md#the-game-window).
`cna_graphics_adapters_refresh` also refuses a valid device with `CNA_RESULT_NOT_SUPPORTED`, because
refresh would invalidate the adapter reference retained by that device.

## Render targets

RenderTarget2D and RenderTargetCube use owned device-child handles. On a game's device they block
game destruction until released; on an independent device, release them before that device.
Creation takes requested color, depth, multisample and usage values; the info query returns native
applied properties. A RenderTarget2D handle is also accepted by the Texture2D info/data/SpriteBatch
routes, matching native inheritance. A cube handle is not a Texture2D; it is accepted by the
TextureCube routes.

The C binding snapshot is committed only after every handle, device owner and subresource is
resolved and native `SetRenderTargets` accepts the transition, including its dimension/MSAA and
other compatibility checks. Count/copy report this C-maintained snapshot; native-only bindings
are not synthesized into handles. Zero bindings restore the backbuffer. A bound target cannot be
disposed or destroyed; unbind it and release any retained references first.

For a 2D binding, `array_slice != 0` returns `CNA_RESULT_NOT_SUPPORTED`, and the unused cube face
must be positive X. For a cube binding, array slice must be zero and the face must be defined;
invalid values return `CNA_RESULT_INVALID_ARGUMENT`.

Creation may succeed when the selected renderer constructs the CNA object but has no actual target
storage. `CNA_RenderTargetInfo.renderer_available` reports whether the native target-renderer
object exists; it does not by itself prove rasterization or pixel correctness. A missing target
renderer makes binding return `CNA_RESULT_NOT_SUPPORTED`.

Native content-loss notification sets `is_content_lost` and raises `ContentLost`. It can come from
renderer loss/recovery or the explicit `cna_graphics_device_notify_content_lost_resources_ext`
route. An ordinary caller-initiated reset does not itself mean content loss. The loss flag clears
where XNA clears it: a draw route or SpriteBatch flush while the target is bound, a successful
clear route while it is bound, or a completed SetData into it. Binding, unbinding or rebinding the
target does not clear it (owner decision 2026-11-04, replacing CABI-28's clear-on-bind).
Rebinding the identical target set is a native no-op; it does not reset a custom
viewport/scissor. The registration handle remains
releasable after target destruction. Native target behavior and pixel evidence are in
[`rendertarget-support.md`](../rendertarget-support.md).

## Sprite fonts

`cna_sprite_font_create` copies a fixed-stride `CNA_SpriteFontGlyph` array containing atlas and
cropping rectangles, one UTF-16 code unit and `CNA_Vector3` kerning per glyph. It validates element
headers, reserved fields and finite kerning/spacing. It retains the caller-owned Texture2D or
RenderTarget2D; generic disposal and typed destruction of the atlas are refused while a font holds
it. A font inherits the texture's device owner; on a game's device the font is also a game child.

Characters and full glyph records have separate count/copy routes; capacities are in elements and
short arrays are not partially written. Default character, line spacing and spacing remain
mutable. The public fallback setter rejects a character absent from the font; construction
preserves the supplied fallback under native content-constructor semantics. A later unresolved
character lookup returns `CNA_RESULT_INVALID_ARGUMENT`.

`cna_sprite_font_measure_utf8` validates and copies length-delimited UTF-8 before native
measurement. Malformed text returns `CNA_RESULT_ENCODING`. Native StringBuilder measurement
delegates to the same string algorithm, so no separate mutable builder handle is needed. Text
submission through SpriteBatch is declared in `graphics.h`; content-loaded fonts are covered in
[`CONTENT.md`](CONTENT.md). Native measurement and encoding details are in
[`spritefont-support.md`](../spritefont-support.md).

## Verification boundary

`CApi_GraphicsResourceSmoke` covers names, C tags, standalone declarations, callback device
identity, disposal timing, registration lifetime, stale/wrong-kind handles and wrong-thread
refusals. `CApi_GraphicsSurfaceSmoke` covers state presets/device round trips, adapter queries
before the first callback, display/presentation snapshots, SpriteBatch begin variants, font
retention/measurement and target binding/refusal. Effect-parameter, skinned-model and render-target
lifetime tests cover additional retention guards. Generated route, ABI and header checks
supplement these semantic tests.

Renderer-dependent branches accept their documented refusal, so a passing C smoke test does not
establish every state's visual effect or target pixels. HEADLESS is a lifecycle/refusal control;
real rendering evidence belongs to the native subsystem tests. Current defects are recorded only
in [KNOWN_ISSUES.md](../../KNOWN_ISSUES.md).
