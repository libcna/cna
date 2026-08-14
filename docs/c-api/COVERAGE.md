# CNA C API Coverage Matrix

## Coverage rule

The final C API covers every public CNA symbol. This matrix is the authoritative evidence for that
claim. A row identifies the source C++ type/member/constant/event, its C-native mapping, owning C
header, implementation/test locations, capability limitations and status.

The matrix began empty at Phase B0. The initial adapter evidence below is deliberately a small
implemented slice, not a substitute for the complete generated inventory required by `CBIND-033`.
Any public symbol without a later inventory row remains **unimplemented**, never implicitly
unsupported or complete.

[`FEATURE_MATRIX.md`](FEATURE_MATRIX.md) summarizes this initial slice for C consumers. This file
remains authoritative for per-symbol implementation evidence.

## Implemented initial adapter slice

| Source header and symbol | C mapping | C header/declaration | Ownership and thread rule | C-only evidence | Status |
|---|---|---|---|---|---|
| `Game` constructor, `RunOneFrame`, `Run`, `Exit`, `Dispose` | Owned generation-checked game handle | `runtime.h`: `cna_game_create`, `cna_game_run_one_frame`, `cna_game_run`, `cna_game_request_exit`, `cna_game_destroy` | One active game; creation-thread calls; destroy is non-reentrant | `LifecycleSmoke.c` | Implemented slice |
| `Game` lifecycle hooks | Copied C callback table and context | `runtime.h`: `CNA_GameCallbacks`, `CNA_CallbackError` | Callback-scoped handle; callback error copied before return | `LifecycleSmoke.c` | Implemented slice |
| `GameTime` getters | Fixed-layout value snapshot | `runtime.h`: `CNA_GameTime` | Borrowed callback value | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |
| `GameWindow::setTitleProperty` | UTF-8 string view | `runtime.h`: `cna_game_set_window_title` | Creation thread; copies text | `LifecycleSmoke.c` | Implemented slice |
| `GraphicsDevice::Clear(Color)` | `CNA_Color` POD + game operation | `core.h`, `runtime.h`: `CNA_Color`, `cna_game_clear` | Creation thread; callback-safe | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |
| `Game::getGraphicsDeviceProperty` | Callback-scoped borrowed handle | `graphics.h`: `cna_game_get_graphics_device` | Valid only during the active lifecycle callback; never caller-released | `LifecycleSmoke.c` | Implemented slice |
| `GraphicsRendererType`, `GraphicsDevice::GetGraphicsRendererType`, `GetGraphicsRendererName`, `GetMaxTextureDimension`, `SupportsCapability` | Stable renderer identity, versioned renderer POD, UTF-8 count/copy and fixed capability query | `graphics.h`: `CNA_GraphicsRendererType`, `CNA_RendererInfo`, `cna_graphics_device_get_renderer_info`, `cna_graphics_device_get_renderer_name_size`, `cna_graphics_device_copy_renderer_name`, `cna_graphics_device_supports_capability` | Callback-scoped borrowed device; creation thread | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |
| `PresentationParameters` backbuffer width/height/format getters, `GraphicsDevice::GetBackBufferData(Color*)` | Versioned logical-backbuffer POD and full RGBA8 count/copy readback | `graphics.h`: `CNA_BackBufferInfo`, `cna_graphics_device_get_backbuffer_info`, `cna_graphics_device_get_backbuffer_data_rgba8` | Callback-scoped borrowed device during draw; creation thread; no partial output | `LifecycleSmoke.c`: HEADLESS honest refusal and SDL_RENDERER exact texture/clear pixels | Getter/readback slice implemented; remaining presentation members/overloads planned |
| `SurfaceFormat` | Stable fixed-width identities for every current canonical enum value | `graphics.h`: `CNA_SurfaceFormat`, `CNA_SURFACE_FORMAT_*` | POD value; only Color transfer is enabled in this slice | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Enum mapped; per-format transfers remain planned |
| `Texture2D(GraphicsDevice&, int, int[, bool, SurfaceFormat])`, width/height/bounds, inherited format/level count | Owned child handle plus versioned create/info PODs | `graphics.h`: `CNA_Texture2DCreateInfo`, `CNA_Texture2DInfo`, `cna_texture2d_create`, `cna_texture2d_get_info` | Create during a game callback; owned texture survives callback and must be destroyed before game | `LifecycleSmoke.c` under HEADLESS and SDL_RENDERER | Color-format slice implemented |
| `Texture2D::SetData(Color*)`, `GetData(Color*)`, `Dispose` | Bulk RGBA8 upload/readback and explicit destroy | `graphics.h`: `cna_texture2d_set_data_rgba8`, `cna_texture2d_get_data_rgba8`, `cna_texture2d_destroy` | Creation-thread calls; exact level-zero count; no partial readback; destroy once | `LifecycleSmoke.c` round-trip, capacity, stale/double-destroy and parent-order checks | Color-format slice implemented |
| `Vector2` fields, `Rectangle` fields | Fixed-layout C value representations | `core.h`: `CNA_Vector2`, `CNA_Rectangle` | POD values copied at calls; methods/operators remain for complete-surface work | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Field layout implemented; methods/operators planned |
| `SpriteSortMode`, `SpriteEffects` | Stable fixed-width identities and effect bits | `graphics.h`: `CNA_SpriteSortMode`, `CNA_SPRITE_SORT_MODE_*`, `CNA_SpriteEffects`, `CNA_SPRITE_EFFECT_*` | POD values validated before native calls | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Complete enum mapping |
| `SpriteBatch(GraphicsDevice&)`, `Begin`, texture `Draw` rectangle overload, `End`, `Dispose` | Owned child handle with versioned begin POD and one bulk POD-command-array call | `graphics.h`: `CNA_SpriteBatchBeginInfo`, `CNA_SpriteCommand`, `cna_sprite_batch_create`, `cna_sprite_batch_begin`, `cna_sprite_batch_submit_many`, `cna_sprite_batch_end`, `cna_sprite_batch_destroy` | Same-game textures retained through active interval; creation-thread calls; active destroy cancels without deferred flush | `LifecycleSmoke.c` under HEADLESS and SDL_RENDERER covers state, validation, lifetime and stale handles | Texture-quad batching slice implemented; custom states/effects/text remain planned |
| `Keys` | Stable fixed-width identities for all 160 canonical enum members | `input.h`: `CNA_Key`, `CNA_KEY_*` | POD values; numeric slots 0–255 accepted by snapshot queries | `CheckKeyIdentities.cmake`, `AbiHeaderC.c`, `AbiHeaderCpp.cpp` | Complete enum mapping |
| `Keyboard::GetState()` | Fresh caller-owned 256-bit snapshot scoped by active game handle | `input.h`: `CNA_KeyboardState`, `cna_keyboard_get_state` | Capture on game creation thread; no per-frame cache; POD survives callback/game | `LifecycleSmoke.c` under HEADLESS and SDL_RENDERER, including wrong-thread refusal | Non-player overload implemented; player overload/extensions planned |
| `KeyboardState::getItem`, `operator[]`, `IsKeyDown`, `IsKeyUp`, `GetPressedKeys` | Local key tests plus ascending count/copy | `input.h`: `cna_keyboard_state_is_key_down`, `cna_keyboard_state_is_key_up`, `cna_keyboard_state_get_pressed_key_count`, `cna_keyboard_state_copy_pressed_keys` | Pure immutable POD queries allowed on any thread; no partial copy | `LifecycleSmoke.c` synthetic multiword snapshot, invalid key and capacity checks | Query slice implemented; equality/hash/string and constructor inventory remain planned |

## Source inventory boundary

`CBIND-033` will generate the complete inventory from public framework headers under:

```text
modules/*/include/Microsoft/**
modules/*/include/CNA/**
```

The scanner excludes `CNA/Internal/**`, generated/test-only artifacts and the C API's own headers.
Any header exposed to a normal CNA application that is not matched by this rule must be added to the
generator input deliberately. The review output records the exact source commit and header/member
count.

For each inventory entry, one of these C mappings is mandatory:

| Mapping status | Required evidence |
|---|---|
| Implemented | C declaration, adapter implementation, C-only positive/negative tests and ABI layout/export tests where applicable. |
| Planned | Target C header/function family, required mapping form and blocking predecessor. |
| Native limitation | A callable C API reports the exact existing CNA renderer/platform limitation; it has a test and owner-approved documentation. |

`Not applicable` is not a valid status for a public CNA symbol. C++ syntax differences are solved by
a C mapping, not by dropping the symbol.

## Required row fields

```text
Source header and symbol
Public family
C mapping (POD / handle / function set / callback / count-copy)
C header and declaration(s)
Ownership and thread rule
Error/capability behavior
C-only test(s)
ABI test(s)
Status
```

`CBIND-043` will make the inventory comparison a required build/CI gate. Adding a new public CNA
symbol must add its coverage row and matching C API work in the same task; deleting or changing a
public symbol must update the mapping intentionally.
