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

## Not yet in this header

Device borrowing and renderer/capability discovery remain in `graphics.h`
(`cna_game_get_graphics_device`, `cna_graphics_device_*`); presentation parameters, display mode and
adapter queries remain in `display.h`; blend/depth-stencil/rasterizer/sampler state remains in
`graphics_state.h`. Device lifetime and events, texture collections, clear/present/reset, buffer
binding, draw submission, SpriteBatch text routes, occlusion queries and the `graphics-ext`
post-process family are owned by CBIND-035F2 through CBIND-035F7 and are not callable yet.
