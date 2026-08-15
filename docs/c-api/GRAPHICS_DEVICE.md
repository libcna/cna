# Graphics Device ABI

`CNA/C/graphics_device.h` is the C surface of the canonical `GraphicsDevice` family. It grows
slice by slice under CBIND-035F; this document states what the header currently guarantees and,
explicitly, what it does not.

CBIND-035F1 establishes the value and identity vocabulary the later device slices consume. It is a
value contract: it does not create, borrow or drive a device, and declaring an identity here does
not advertise renderer support for the operation that consumes it.

## Fixed-layout viewport

`CNA_Viewport` is a 24-byte POD with `int32_t x`, `y`, `width`, `height` and `float min_depth`,
`max_depth`, in that order. Those six fields are the complete public property set of the canonical
`Viewport`, so reading and writing them directly is the C mapping of its getters and setters — the
header adds no `cna_viewport_get_x`-style accessors for values a C caller already owns.

Three initializers represent the three constructors:

| C operation | Canonical constructor | Result |
|---|---|---|
| `cna_viewport_init` | `Viewport()` | zero position/size, `min_depth` 0, `max_depth` 1 |
| `cna_viewport_init_bounds` | `Viewport(int, int, int, int)` | given position/size, `min_depth` 0, `max_depth` 1 |
| `cna_viewport_init_from_rectangle` | `Viewport(const Rectangle&)` | rectangle position/size, `min_depth` 0, `max_depth` 1 |

The default is deliberately not an all-zero structure: the canonical default constructor leaves
`MaxDepth` at 1, and the C route reports exactly what CNA produces rather than what a zeroed POD
would suggest. A caller that memsets a `CNA_Viewport` to zero therefore gets a degenerate depth
range, not the framework default.

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

Every operation is a pure value transform: no handle, no allocation, no thread affinity, and a null
output pointer is `CNA_RESULT_INVALID_ARGUMENT` with no partial write.

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

CBIND-035F2 adds the device's own contract on top of the borrowed handle that `graphics.h`
already provides.

### Lifetime is the game's, not the caller's

There is no `cna_graphics_device_create` or `cna_graphics_device_destroy`. The canonical
`GraphicsDevice` is created by the running `Game` from its adapter, profile and presentation state,
and destroyed when `cna_game_destroy` runs. `cna_game_get_graphics_device` borrows it for the
duration of one lifecycle callback; the handle is generation-invalidated the moment that callback
returns.

`cna_graphics_device_dispose` exists so the rule is callable rather than merely documented: given a
valid device handle it returns `CNA_RESULT_NOT_SUPPORTED` and explains that the active game owns
the device. Disposal is observed, not performed, through
`cna_graphics_device_get_is_disposed`.

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
exception firewall, so it applies to every C entry point, not just the device family. Only the
DirectX 9 backend currently raises these conditions, so the conversion is proven by a focused
adapter test rather than by a HEADLESS or SDL_RENDERER device.

`IGraphicsDeviceService` is an abstract C++ interface. C can neither implement nor instantiate it;
`cna_game_get_graphics_device` is the C route to the device it exposes. Its four service-level
events belong to the canonical `GraphicsDeviceManager` implementation in the runtime module and
remain owned by CBIND-037.

## Texture sampler collections

A device owns two `TextureCollection` objects — one for pixel-shader samplers and one for
vertex-shader samplers. Both hold raw `Texture*` slots, which cannot cross the ABI, so the C API
addresses them by stage and slot instead of handing out a collection object:

| C operation | Canonical member |
|---|---|
| `cna_graphics_device_get_texture` | `Textures`/`VertexTextures` `operator[]` |
| `cna_graphics_device_set_texture` | `Textures`/`VertexTextures` `operator()` |
| `cna_graphics_device_unbind_texture` | `RemoveDisposedTexture` |

`CNA_TEXTURE_COLLECTION_MAX_TEXTURES` is 16 and is asserted against the canonical
`TextureCollection::MaxTextures` at compile time. Any slot at or above it, and any stage other than
`CNA_SHADER_STAGE_PIXEL` or `CNA_SHADER_STAGE_VERTEX`, is `CNA_RESULT_INVALID_ARGUMENT`.

A read fills a versioned `CNA_TextureSlotInfo`. Its two fields answer two different questions:
`bound` says whether the native slot holds a texture at all, and `texture` names the C handle that
bound it. They diverge when canonical CNA code fills the slot itself — a SpriteBatch flush, for
instance — in which case `bound` is `CNA_TRUE` and `texture` is `CNA_INVALID_HANDLE`, because no C
resource owns that texture and inventing a handle for it would be a lie.

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
written, and a backend without honest readback returns `CNA_RESULT_NOT_SUPPORTED` with the
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
caller's array the pixels land. Two capacity rules are decided in C, before any native read: the
array must hold `start_index + element_count` pixels, and `element_count` must cover the whole
selected region. Both report `CNA_RESULT_BUFFER_TOO_SMALL`; the canonical routine would otherwise
surface an undersized count as a generic native failure. Nothing outside the requested window is
written, and a backend without honest readback returns `CNA_RESULT_NOT_SUPPORTED` with the
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

## Not yet in this header

Renderer/capability discovery remains in `graphics.h`; presentation parameters, display mode and
adapter queries remain in `display.h`; blend/depth-stencil/rasterizer/sampler state remains in
`graphics_state.h`. Draw submission and the CNAEXT device helpers, SpriteBatch text routes, occlusion queries and the
`graphics-ext` post-process family are owned by CBIND-035F5 through CBIND-035F7 and are not
callable yet.
