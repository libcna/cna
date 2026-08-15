# C Graphics Resources, States, Display Values, Render Targets and Sprite Fonts

## Scope

The CBIND-034 surface is declared by `graphics_state.h`, `display.h`, `render_target.h` and
`sprite_font.h`, all included by `CNA/C/cna.h`. It maps the complete public CNA families for
graphics state values, sampler collections, display modes/adapters, presentation parameters,
render targets and SpriteFont without exposing a C++ object, collection or renderer pointer.
CBIND-035C3 adds the shared `GraphicsResource` contract in `graphics_resource.h` for every
currently supported resource handle.

The completed Texture/Texture2D construction, typed-transfer and encoded-image contract is
documented separately in [`TEXTURES.md`](TEXTURES.md).

All extensible inputs and outputs begin with `struct_size` and `struct_version`. Version one
callers initialize both fields before a query; preset/value initializers fill the complete output.
Fixed-width identities and layouts are frozen by both C17 and C++23 compile-time assertions.

## Common graphics-resource contract

Texture2D, RenderTarget2D, RenderTargetCube and VertexDeclaration handles support the generic
`cna_graphics_resource_*` operations. A wrong-kind or stale handle is rejected before the native
object is touched. Standalone declarations report an invalid graphics-device handle; game-owned
resources return the same borrowed device handle only during an active lifecycle callback. The
borrow expires with that callback.

Names are validated length-delimited UTF-8. Count/copy operations return exact bytes without an
implicit terminator and never write a partial result. `ToString` returns the name when nonempty and
the native fully qualified type name otherwise. The C tag is a fixed 64-bit opaque token stored in
the validated handle registry; it deliberately does not expose or mutate the native
`System::Object* Tag`, and it resets when a handle slot is released and reused.

Explicit disposal keeps the C handle alive so callers may query `is_disposed`; repeated disposal
is a successful no-op. Typed destroy operations dispose before releasing their handle. A disposing
subscription owns a separate registration handle and invokes its caller-owned function/context
synchronously before native disposal state changes. Unsubscription remains valid after the
resource itself is destroyed. Resource operations and subscriptions retain the creation-thread
rule.

## Graphics state values

`CNA_BlendState`, `CNA_DepthStencilState`, `CNA_RasterizerState` and `CNA_SamplerState` are copied
POD descriptors. Their `cna_*_state_init` functions materialize the native default and named XNA
presets. A caller may then change fields and use the matching graphics-device get/set operation.
Sampler operations address one of 16 pixel or vertex slots with `CNA_ShaderStage`.

Preset initialization is renderer-independent and may run on any thread. Applying/querying a
device state requires the callback-scoped graphics-device handle on the game creation thread.
Native renderer limitations are returned through the ordinary exception barrier; a backend may
reject a state it cannot represent. `cna_sprite_batch_begin_with_states` maps the state-bearing
Begin overload while Effect and transform-matrix variants remain assigned to CBIND-035.

## Display, adapters and presentation

`CNA_DisplayMode` and `CNA_PresentationParameters` are value snapshots. Display modes support
initialization and equality. Adapter collections use count/copy APIs, optional SurfaceFormat
filtering and index-based metadata/string/profile/format queries. No C++ iterator or string is
retained. Description and device-name copies contain exactly the reported UTF-8 byte count and no
implicit terminator.

Presentation initialization uses native defaults; clone deliberately executes CNA's native Clone
behavior. A device set preserves CNA's already-owned native window internally. Native monitor and
window handles never cross this ABI: their query functions zero the output and return
`CNA_RESULT_NOT_SUPPORTED`. Adapter refresh is also a callable limitation while a C-owned game is
active because CNA's current device retains an adapter pointer that refresh would invalidate.

## Render targets

RenderTarget2D and RenderTargetCube use owned game-child handles. Creation records requested color,
depth, multisample and usage values; the info query returns native applied properties. A
RenderTarget2D handle is also accepted by the existing Texture2D info/data/SpriteBatch routes,
matching native inheritance. A cube handle is not a Texture2D.

The active binding array is replaced atomically only after every handle, parent game, subresource,
dimension and applied sample count passes validation and CNA accepts the native transition. Count
and copy return the C binding snapshot; zero bindings restore the backbuffer. A bound target cannot
be destroyed. Every target must be unbound and destroyed before its game.

Creation may succeed when the selected backend constructs the CNA object but has no actual target
storage. `CNA_RenderTargetInfo.renderer_available` then returns false and binding reports
`CNA_RESULT_NOT_SUPPORTED`. Native `ContentLost` is currently never raised, so
`is_content_lost` is the explicit false invariant. Renderer-pointer extensions are represented
only by `renderer_available`; no native pointer is disclosed.

## Sprite fonts

`cna_sprite_font_create` copies an exact-stride glyph table containing rectangles, one UTF-16 code
unit and `CNA_Vector3` kerning per glyph. It retains the caller-owned Texture2D or RenderTarget2D;
the texture handle cannot be destroyed until every font retaining it is destroyed. The font itself
is an owned game child and must be destroyed before the game.

Characters are exposed through count/copy semantics. Default character, line spacing and spacing
remain mutable. `cna_sprite_font_measure_utf8` validates and copies length-delimited UTF-8 before
calling native measurement; it covers both native String and StringBuilder overload semantics.
Malformed UTF-8 returns `CNA_RESULT_ENCODING`, and a fallback character not present in the font
returns `CNA_RESULT_INVALID_ARGUMENT`.

## Verified configurations

`GraphicsSurfaceSmoke.c` and `GraphicsResourceSmoke.c` are strict C17 and run unchanged against
HEADLESS and SDL_RENDERER with SDL dummy video. They cover common names, tags, device identity,
disposal events and lifetime, state presets and device round trips, SpriteBatch state application,
display/adapter/presentation snapshots, Texture2D/SpriteFont retention, render-target creation and
binding/refusal, stale handles, parent ordering and wrong-thread access. HEADLESS is the explicit
unsupported-storage control; SDL_RENDERER exercises real 2D target storage where available.
