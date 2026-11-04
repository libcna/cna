# Graphics Device ABI

`CNA/C/graphics_device.h` exposes viewport values, graphics-device state and events, frame control,
buffer and texture bindings, draw submission, occlusion queries and device extensions. The device
may be borrowed from a game or independently owned by C. Declaring an operation or identity does
not advertise renderer support; query the active device before choosing an optional path.

Renderer discovery and detailed capability queries are in
[`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md). Shared ownership, threading and
error rules are in [`OWNERSHIP.md`](OWNERSHIP.md),
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md) and [`ERRORS.md`](ERRORS.md).

## Fixed-layout viewport

`CNA_Viewport` is a 24-byte POD with `int32_t x`, `y`, `width`, `height` and `float min_depth`,
`max_depth`, in that order. Those six fields are the complete public property set of the canonical
`Viewport`, so reading and writing them directly is the C mapping of its getters and setters — the
header adds no `cna_viewport_get_x`-style accessors for values a C caller already owns.

Three initializers represent the three constructors:

| C operation | Canonical constructor | Result |
|---|---|---|
| `cna_viewport_init` | `Viewport()` / XNA `default(Viewport)` | all six fields zero |
| `cna_viewport_init_bounds` | `Viewport(int, int, int, int)` | given position/size, `min_depth` 0, `max_depth` 1 |
| `cna_viewport_init_from_rectangle` | `Viewport(const Rectangle&)` | rectangle position/size, `min_depth` 0, `max_depth` 1 |

The default preserves XNA's zero-initialized value type, including `MaxDepth = 0`. Only the two
parameterized constructors establish the usual 0–1 depth range. Initialize from bounds when that
range is required; a zeroed `CNA_Viewport` has the same values as `cna_viewport_init`.

`cna_viewport_get_aspect_ratio` returns width divided by height, or zero when either dimension is
zero — the native guard, not an error. `cna_viewport_get_bounds`, `cna_viewport_set_bounds` and
`cna_viewport_get_title_safe_area` delegate to the canonical property implementations; setting
bounds replaces position and size and leaves the depth range untouched, and the title-safe area is
the same rectangle as the bounds.

`cna_viewport_project` and `cna_viewport_unproject` take the source point and the three matrices by
value and write one `CNA_Vector3`. They perform the canonical perspective divide, viewport scaling
and depth-range mapping; no matrix is mutated and no native object is created.

Viewport text uses the standard count/copy contract: `cna_viewport_get_string_size` reports the
exact UTF-8 byte count without a terminator, and `cna_viewport_copy_string` either writes the whole
string or writes nothing and returns `CNA_RESULT_BUFFER_TOO_SMALL` with the requirement in
`out_bytes`. The format is the canonical
`{X:… Y:… Width:… Height:… MinDepth:… MaxDepth:…}`, with the depth values in the native
six-decimal form.

These operations require no handle or device thread. Formatting text may allocate internally;
returned bytes use caller-owned storage. A null required output returns
`CNA_RESULT_INVALID_ARGUMENT`; the copy route reports the required size without writing a short
buffer prefix.

## Stable identities

| C identity | Canonical source | Values |
|---|---|---|
| `CNA_ClearOptions` | `ClearOptions` | `TARGET` 1, `DEPTH_BUFFER` 2, `STENCIL` 4 |
| `CNA_GraphicsDeviceStatus` | `GraphicsDeviceStatus` | `NORMAL` 0, `LOST` 1, `NOT_RESET` 2 |
| `CNA_Unsupported3DGraphicsCallBehavior` | `CNA::Unsupported3DGraphicsCallBehavior` | `THROW` 0, `WARN_AND_STUB` 1 |

Each constant equals its native ordinal, and the adapter sources assert that equality at compile
time, so a divergence in the canonical enumeration fails the build rather than silently shifting
an ABI value.

## Bitwise operators need no adapter

`ClearOptions` and `SpriteEffects` declare `|`, `&`, `~`, `|=` and `&=` overloads so that scoped C++
enumerations can be combined. `CNA_ClearOptions` and `CNA_SpriteEffects` are fixed-width unsigned
integers carrying the identical bits, so C's own operators are the mapping:

```c
CNA_ClearOptions options = CNA_CLEAR_OPTION_TARGET | CNA_CLEAR_OPTION_DEPTH_BUFFER;
options &= ~CNA_CLEAR_OPTION_TARGET;   /* leaves CNA_CLEAR_OPTION_DEPTH_BUFFER */
```

Exporting `cna_clear_options_or`-style functions would add an ABI call for something the language
already expresses exactly. The strict-C test exercises every combination so the claim is checked,
not assumed.

## Device lifetime, state and events

### Borrowed and caller-owned lifetimes

`cna_game_get_graphics_device` borrows the game's device during a lifecycle callback or before the
first run, step or tick. A callback loan ends when that callback returns; a pre-run loan ends when
the first callback returns. Repeated queries within a validity window return the same handle, and
an out-of-callback query after running has begun is refused. The game creates and disposes its
device. The complete borrowing contract is in
[`RENDERERS_AND_CAPABILITIES.md`](RENDERERS_AND_CAPABILITIES.md#borrowed-graphics-device-handle).

`cna_graphics_device_create` instead constructs an independent device outside any `Game` from an
adapter index, graphics profile and presentation parameters. Its handle is owned until
`cna_graphics_device_destroy`; several independently owned devices may coexist, and the ordinary
device operations and resource-creation routes accept either handle form. Resources carry their
creating-device identity; routes that combine resources enforce their documented same-device
rules. Destroy a device's resources before releasing the device. Standalone-device resources do
not count as game children and do not block an unrelated `cna_game_destroy`.

`cna_graphics_device_dispose` does not transfer or release either kind of handle. It returns
`CNA_RESULT_NOT_SUPPORTED` for a valid device: a borrowed device belongs to its game, while a
caller-owned device must be disposed and released atomically through
`cna_graphics_device_destroy`. `cna_graphics_device_get_is_disposed` observes the native disposal
state.

### State

| C operation | Canonical property |
|---|---|
| `cna_graphics_device_get_is_disposed` | `IsDisposed` |
| `cna_graphics_device_get_status` | `GraphicsDeviceStatus` |
| `cna_graphics_device_get_adapter_index` | `Adapter` |
| `cna_graphics_device_get_graphics_profile` | `GraphicsProfile` |
| `cna_graphics_device_get`/`set_scissor_rectangle` | `ScissorRectangle` |
| `cna_graphics_device_get`/`set_viewport` | `Viewport` |
| `cna_graphics_device_get`/`set_blend_factor` | `BlendFactor` |
| `cna_graphics_device_get`/`set_multi_sample_mask` | `MultiSampleMask` |
| `cna_graphics_device_get`/`set_reference_stencil` | `ReferenceStencil` |
| `cna_graphics_device_get_type_name_size` / `_copy_type_name` | `GetTypeName()` |

`Adapter` is a native `GraphicsAdapter&`, which must not cross the ABI, so the C route returns the
adapter's index into the current enumeration — the same index the `cna_graphics_adapter_*` queries
in `display.h` take. Indices are point-in-time: an adapter change renumbers them, and a device whose
adapter is no longer enumerated reports `CNA_RESULT_INVALID_STATE` rather than a stale index.

`cna_graphics_device_set_viewport` rejects a non-finite depth range with
`CNA_RESULT_INVALID_ARGUMENT` before touching the device, so a rejected call leaves the current
viewport intact. The canonical defaults are preserved exactly: `MultiSampleMask` starts at -1 and
`ReferenceStencil` at 0.

### Events

Four device events carry no payload and share one entry point:

```c
CNA_GraphicsDeviceEventRegistrationHandle registration = CNA_INVALID_HANDLE;
cna_graphics_device_subscribe_event(
    device, CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESET, on_reset, context, &registration);
/* … */
cna_graphics_device_unsubscribe(registration);
```

`ResourceCreated` and `ResourceDestroyed` carry payloads and have their own entry points, each
delivering a versioned structure that is valid only for the duration of the callback.

Two payload decisions are deliberate limitations, not omissions:

- `ResourceCreatedEventArgs::Resource` is **not** exposed, not even as a type name. The canonical
  event is raised from the `GraphicsResource` base constructor, so the reported object has no
  concrete type yet and querying any virtual member of it would be a pure-virtual call. The C
  event reports only that a resource was supplied.
- `ResourceDestroyedEventArgs::Tag` is caller-owned native state of unknown liveness, so it too is
  reported as presence only. The resource `Name` is a canonical string and is passed as a
  callback-scoped UTF-8 view.

Neither payload ties an event to a C handle or exposes a stable lifecycle ID. Such an identity
would need a separately versioned extension; consumers cannot infer one from these version-1
payloads.

A subscription is an owned handle. It does **not** block `cna_game_destroy`: destroying the game
disposes the device — which is exactly when a `Disposing` subscriber wants to be called — and the
runtime then invalidates every live subscription before the device object goes away. Surviving
registration handles stay releasable afterwards; releasing one twice returns
`CNA_RESULT_INVALID_HANDLE`.

### Device exceptions never cross the ABI

`DeviceLostException` and `DeviceNotResetException` become `CNA_RESULT_INVALID_STATE` with
`CNA_ERROR_CATEGORY_STATE`; `NoSuitableGraphicsDeviceException` becomes `CNA_RESULT_NOT_SUPPORTED`
with `CNA_ERROR_CATEGORY_NOT_SUPPORTED`. The exception message — default or custom — reaches the
caller through the ordinary per-thread UTF-8 diagnostic. The conversion lives in the shared
exception firewall, so it applies to every C entry point, not just the device family.
`CApi_BoundaryDetail` independently checks the exception translation without requiring a driver
fault.

`IGraphicsDeviceService` is an abstract C++ interface. C can neither implement nor instantiate it;
`cna_game_get_graphics_device` is the C route to the device it exposes. Its four service-level
events belong to the canonical `GraphicsDeviceManager` implementation in the runtime module and
are exposed by the manager subscriptions documented in
[`GAME_COMPONENTS.md`](GAME_COMPONENTS.md#the-graphics-device-manager).

## Texture sampler collections

A device owns two `TextureCollection` objects — one for pixel-shader samplers and one for
vertex-shader samplers. Both hold raw `Texture*` slots, which cannot cross the ABI, so the C API
addresses them by stage and slot instead of handing out a collection object:

| C operation | Canonical member |
|---|---|
| `cna_graphics_device_get_texture` | `Textures`/`VertexTextures` `operator[]` |
| `cna_graphics_device_set_texture` | `Textures`/`VertexTextures` `operator()` |
| `cna_graphics_device_unbind_texture` | `RemoveDisposedTexture` |

`CNA_TEXTURE_COLLECTION_MAX_TEXTURES` is 16 and is asserted against the canonical storage maximum
at compile time. Any slot at or above it, and any stage other than `CNA_SHADER_STAGE_PIXEL` or
`CNA_SHADER_STAGE_VERTEX`, is `CNA_RESULT_INVALID_ARGUMENT`. The native collection additionally
limits vertex sampling to four slots in HiDef and none in Reach; reading an index within the
storage maximum but outside that active range returns `CNA_RESULT_INVALID_ARGUMENT` with
`CNA_ERROR_CATEGORY_RANGE`. Vertex texture
formats must also satisfy the native vertex-sampling rules.

A read fills a versioned `CNA_TextureSlotInfo`. Its two fields answer two different questions:
`bound` says whether the native slot holds a texture at all, and `texture` names a still-live C
handle recorded by the C binding route for that exact native texture. A native binding with no
matching record can therefore report `bound = CNA_TRUE` and `texture = CNA_INVALID_HANDLE`, even
when the texture exists elsewhere under a C handle. This query does not create a handle or acquire
ownership.

Binding takes any owned texture handle: Texture2D, Texture3D, TextureCube or either render-target
type. Passing `CNA_INVALID_HANDLE` empties the slot. Binding stores no ownership and no reference
count: destroying a texture through its own C route unbinds it from every slot first, exactly as
canonical disposal does, so a destroyed texture can never be left addressable through a sampler.
Binding a texture that is currently bound as a render target is `CNA_RESULT_INVALID_STATE`, the
canonical rule.

There is no standalone C texture collection. The canonical public default constructor produces a
deviceless collection that no device ever consults, so exposing it would add an object with no
observable effect.

## Frame control and buffer binding

Three clear routes cover the canonical overloads that `cna_game_clear` does not:
`cna_graphics_device_clear_rgba` takes four floating-point channels,
`cna_graphics_device_clear_color_depth` clears color and depth together, and
`cna_graphics_device_clear_options` takes a `CNA_ClearOptions` mask with a depth and stencil value.
Non-finite channels or depths, and unknown option bits, are `CNA_RESULT_INVALID_ARGUMENT` before the
device is touched. `cna_graphics_device_present` presents the frame.

`cna_graphics_device_reset` reuses the device's current presentation parameters;
`cna_graphics_device_reset_with_parameters` takes new ones plus a **nullable** `adapter_index`
pointer. That single pointer expresses both canonical overloads: a null pointer is the
keep-the-current-adapter form, a non-null one selects an adapter by the same index the
`cna_graphics_adapter_*` queries use. The renderer-private device window handle is preserved across
the reset, because a C caller never supplies it. A successful reset raises the device's resetting
and reset events in that order, which a C subscriber observes.

`cna_graphics_device_get_backbuffer_data_window` reads a window of the back buffer through a
versioned `CNA_BackBufferReadback`. The canonical nullable `Rectangle*` becomes an explicit
`has_source_rectangle` flag plus a value, and `start_index`/`element_count` describe where in the
caller's array the pixels land. Two capacity rules are decided in C, before any native read:
the array must hold `start_index + element_count` pixels, and `element_count` must cover the whole
selected region. Both report `CNA_RESULT_BUFFER_TOO_SMALL`; the canonical routine would otherwise
surface an undersized count as a generic native failure. Nothing outside the requested window is
written, and a renderer without honest readback returns `CNA_RESULT_NOT_SUPPORTED` with the
destination untouched.

Buffer binding replaces the native pointer-and-vector surface:

| C operation | Canonical member |
|---|---|
| `cna_graphics_device_set_vertex_buffer` / `_set_vertex_buffer_offset` | both `SetVertexBuffer` overloads |
| `cna_graphics_device_set_vertex_buffers` | `SetVertexBuffers` |
| `cna_graphics_device_get_vertex_buffer_count` / `_copy_vertex_buffers` | `GetVertexBuffers` |
| `cna_graphics_device_get_vertex_buffer` | `GetVertexBuffer` |
| `cna_graphics_device_set_index_buffer` / `_get_index_buffer` | `SetIndexBuffer`, `GetIndexBuffer`, `Indices` and the `Indices` property pair |

The four canonical index-buffer accessors are one operation in CNA, so they collapse to one
validated get/set pair. `cna_graphics_device_set_vertex_buffers` validates every binding — a
negative vertex offset or instance frequency, or an unusable handle — before applying any of them,
so a rejected call leaves the previous binding set intact. Reads report the owning C handle; as with
sampler slots, a binding applied by canonical CNA code reports `CNA_INVALID_HANDLE` rather than
inventing one.

## Draw submission

Three routes draw from the currently bound buffers — `cna_graphics_device_draw_primitives`,
`_draw_indexed_primitives` and `_draw_instanced_primitives` — and
`cna_primitive_type_get_vertex_count` exposes the canonical topology-to-vertex-count helper so a
caller can size an array without attempting a draw.

The native user-primitive overloads vary by vertex representation, explicit declaration and index
width. The C surface uses two functions and versioned descriptors for those dimensions:

```c
CNA_UserPrimitives primitives = {
    sizeof(CNA_UserPrimitives), 1u, CNA_PRIMITIVE_TRIANGLE_LIST,
    CNA_USER_VERTEX_SOURCE_POSITION_COLOR, vertices, CNA_INVALID_HANDLE, 0, 3, 1, 0u};
cna_graphics_device_draw_user_primitives(device, &primitives);
```

A built-in vertex source is **converted** into native values before submission. That is not an
optimization choice: `VertexPositionColor` and its siblings embed a polymorphic `Color`, so a C
array of the matching POD is never layout-compatible with the native structure. For the same
reason `CNA_USER_VERTEX_SOURCE_RAW_STREAM` always requires an explicit declaration — the canonical
declaration-less raw overloads read their bytes as an array of native objects, which C cannot
produce. The C-safe equivalent of those overloads is `CNA_USER_VERTEX_SOURCE_POSITION_COLOR`
without a declaration, which produces the same result through the conversion.

After validating its arguments and handles, every draw route checks the device's 3D capability
before native submission. A device without that capability returns `CNA_RESULT_NOT_SUPPORTED`.
Indexed user draws use `CNA_UserIndices` to select 16- or 32-bit indices and an index offset.

## Device extensions

`cna_graphics_device_get_tracked_resource_count`, the depth-test/blend/depth-write toggles, the
graphics-profile and context-recovery setters, the debug-marker route, the unsupported-3D policy
pair, current-effect assignment and the multisample renderer rebuild map CNA's own device
extensions. The three pipeline-state toggles are capability-gated like the draw routes.

`cna_graphics_device_set_current_effect` needs one extra guarantee the canonical API does not give:
the device stores a borrowed `Effect*`, so the C API retains the assigned effect independently of
its handle. Clearing or replacing the assignment releases that reference. Destroying the effect's
own handle while it is current therefore cannot leave the device pointing at freed memory.

`OnResourceCreated`, `OnResourceDestroyed`, `AddResourceReference`, `RemoveResourceReference` and
`GetRenderer` have **no** C route, because each takes or returns a native object: a partially
constructed `System::Object*`, a `GraphicsResource*`, or the renderer itself. Their observable
effects are already reachable — resource events through the subscriptions above, tracking
automatically for every owned C resource and readable through the tracked-count query, and renderer
identity and capabilities through `graphics.h`.

The later device extensions are available in the same header:

- `cna_graphics_device_notify_content_lost_resources_ext` notifies a snapshot of content-losable
  resources, allowing callbacks to dispose resources safely during notification;
- `cna_graphics_device_supports_surface_format_as_render_target_ext` asks the format-acceptance
  predicate used by render-target construction;
- `cna_graphics_device_executes_shader_effect_source_ext` distinguishes accepting a shader effect
  from executing its source, and `cna_graphics_device_supports_image_based_lighting_ext` queries the
  renderer's IBL path. A broad `CUSTOM_EFFECTS` capability is insufficient for either guarantee.

### Display color space

`CNA_DisplayColorSpace` defines `SRGB`, `SCRGB` and `HDR10` as values 0, 1 and 2. The get route
reports the active space; the set route returns `CNA_RESULT_SUCCESS` with `out_changed = CNA_FALSE`
when a defined space is unavailable. Undefined identities return `CNA_RESULT_INVALID_ARGUMENT`.

`cna_graphics_device_supports_display_color_space_ext` may try the requested space and restore the
previous one. It is an active probe and can cause a momentary visible change; an already-active
space needs no transition. This answer describes the live renderer/display, not a permanent
property of its renderer identity.

## Occlusion queries

`cna_occlusion_query_create` creates an owned query for the supplied device. On a game-owned device
it is a game child and blocks game destruction until released. Creation rejects Reach and a device
without the occlusion-query capability. The handle is a full graphics resource: the generic
`cna_graphics_resource_*` name, string, tag, device, disposal and disposing-subscription routes
accept it, which is where the canonical protected `Dispose(bool)` and `GetTypeName` behavior is
observed; as in XNA, its disposing subscription is never invoked.
`cna_occlusion_query_has_renderer` reports the native object's presence and
`cna_occlusion_query_destroy` disposes and releases the handle.

The begin/end/completion/pixel-count routes preserve XNA's state machine: end without begin,
nested begin, repeated end and reuse before a completion observation return
`CNA_RESULT_INVALID_STATE`. Reading the pixel count also observes completion and refuses a result
that is not ready. Distinct queries may overlap independently. Observing completion permits reuse
even when the observation returned false; it does not make the previous pixels available.
A query disposed through `cna_graphics_resource_dispose` keeps that state machine, as XNA's does:
completion and the precision flag report `CNA_FALSE`, the pixel count returns
`CNA_RESULT_INVALID_STATE`, and begin/end return `CNA_RESULT_INVALID_STATE` -- for their ordinary
refusal, or because the native query is gone (XNA's `NullReferenceException`, which the C ABI
reports as a state failure).

`cna_occlusion_query_get_is_pixel_count_precise_ext` distinguishes a sample tally from a visibility
answer. Read it alongside the completed result because a renderer may classify its native query
on first begin. For an imprecise result, use only zero versus non-zero; dividing by an area does
not yield coverage. Renderer-specific precision and HEADLESS's validation sentinel are documented
in [`occlusionquery-support.md`](../occlusionquery-support.md).

## Related headers and verification

Renderer/capability discovery remains in `graphics.h`; presentation parameters, display mode and
adapter queries remain in `display.h`; blend/depth-stencil/rasterizer/sampler state remains in
`graphics_state.h`. SpriteBatch itself lives in `graphics.h`, including its text and mesh routes.
The post-processing, ASCII and other optional graphics extensions are already exposed through
`graphics_ext.h`; see [`GRAPHICS_EXT.md`](GRAPHICS_EXT.md).

`CApi_GraphicsDeviceSmoke` covers viewport defaults and transforms, device state/events, sampler
and buffer bindings, clear/reset/readback, draw submission and query acceptance/state. On SDLGPU
its occlusion branch verifies the capability refusal; renderer query and pixel evidence belongs to
the native subsystem tests. `CApi_GraphicsExtSmoke` covers the later device extensions, including
color-space identity validation and optional post-processing. The owned-device, concurrent-device
and secondary-context smoke tests exercise independent device lifetimes. `CApi_LifecycleSmoke`
and `CApi_RuntimeComponentDeviceBorrow` cover callback borrowing; `CApi_BoundaryDetail` covers
exception translation. These tests complement the generated ABI and boundary checks; one passing
renderer configuration does not qualify every platform or driver.
