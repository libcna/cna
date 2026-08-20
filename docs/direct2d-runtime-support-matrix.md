# Direct2D runtime support matrix

plans/plan_direct2d.md D2D-130. Every Direct2D branch that can behave differently per runtime is listed
here with one of four dispositions, and nothing is covered by a global "this runtime is
unsupported" skip:

| Disposition | Meaning |
|---|---|
| **supported** | The branch runs and its result is asserted on that runtime. |
| **fallback** | The branch is unavailable, and a defined alternative path produces the same public result. |
| **named reject** | The branch is unavailable and CNA fails with a named `NotSupportedException` naming the missing capability. A raw HRESULT is never surfaced. |
| **open task** | Not yet demonstrated on that runtime; the plan row that owns the evidence is named. |

Runtimes: **Windows** is native x64 Windows Direct2D; **Wine** is Wine 10.0's `d2d1` on WineD3D;
**Proton** is the pinned runtime from `scripts/direct2d-proton-pin.txt` (Wine's `d2d1` on DXVK);
**WARP** is native Windows with `CNA_DIRECT2D_FORCE_WARP=1`.

## Drawing and sampling

| Branch | Windows | WARP | Wine | Proton |
|---|---|---|---|---|
| `DrawImage` of an undecorated bitmap (source-over) | supported | supported | supported | supported |
| `ID2D1ImageBrush` sampling: crop, flips, `Clamp`/`Wrap`/`Mirror` | supported | supported | supported | supported |
| All nine `TextureFilter` spatial modes | supported | supported | supported | supported |
| Authored `Texture2D` mip selection and mip-linear blending | supported | supported | supported | supported |
| `ColorMatrix` tint of an ordinary `Texture2D` | open task (D2D-22) | open task (D2D-22) | fallback (`MakeSpritePixels`) | fallback (`MakeSpritePixels`) |
| `Premultiply` of a straight-alpha `Texture2D` | open task (D2D-22) | open task (D2D-22) | fallback (`MakeSpritePixels`) | fallback (`MakeSpritePixels`) |
| `ColorMatrix`/`Premultiply` of a `RenderTarget2D` source | open task (D2D-22) | open task (D2D-22) | named reject | named reject |
| Sampling the currently bound render target | named reject | named reject | named reject | named reject |

A render target has no CPU shadow, which is why the decorated render-target path is a named reject
rather than a fallback: there is nothing to fall back to. The test skip variable for that exact
branch is `CNA_DIRECT2D_SKIP_RENDER_TARGET_DECORATION`.

## Compositing

| Branch | Windows | WARP | Wine | Proton |
|---|---|---|---|---|
| `AlphaBlend`, `NonPremultiplied` (source-over) | supported | supported | supported | supported |
| `Opaque` (`D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY`) | open task (D2D-22) | open task (D2D-22) | open task (D2D-22) | open task (D2D-22) |
| Exact Porter-Duff composite modes (D2D-33) | open task (D2D-22) | open task (D2D-22) | open task (D2D-22) | open task (D2D-22) |
| `BlendState::Additive` | named reject | named reject | named reject | named reject |
| Blend factor / channel mask / coverage mask | named reject | named reject | named reject | named reject |

WineD3D and Proton's Wine ignore the non-source-over `DrawImage` composite modes rather than
failing, so their results for those branches are not evidence either way. The test skip variable
for exactly those branches is `CNA_DIRECT2D_SKIP_ADVANCED_BLEND`. Neither skip variable disables a
whole test executable.

## Presentation and device

| Branch | Windows | WARP | Wine | Proton |
|---|---|---|---|---|
| Logical framebuffer, all five presentation transforms, resize | supported | supported | supported | supported |
| Physical swap-chain output of letterbox bars / overscan crop | open task (D2D-57, D2D-58, D2D-126) | open task (D2D-126) | open task (D2D-126) | open task (D2D-126) |
| Multi-monitor and non-96 DPI behavior | open task (D2D-23, D2D-55, D2D-126) | open task | open task | open task |
| Hardware device creation | supported | forced software | supported | supported |
| WARP fallback when no hardware adapter exists | supported | n/a | supported | supported |
| Device-loss classification and recovery | supported | supported | supported | supported |
| `ID2D1Bitmap::CopyFromRenderTarget` readback | supported | supported | fallback (`CopyFromBitmap`) | fallback (`CopyFromBitmap`) |
| In-place `CopyFromMemory` level upload | supported | supported | fallback (create-and-swap) | fallback (create-and-swap) |
| D3D11/D2D debug layers and live-object report | supported | supported | open task | open task |

## Skip variables

Exactly two environment variables narrow what a compatibility run asserts, and each names one
branch rather than a runtime:

| Variable | Branch it withholds |
|---|---|
| `CNA_DIRECT2D_SKIP_RENDER_TARGET_DECORATION` | Decorated (`ColorMatrix`/`Premultiply`) render-target source pixels |
| `CNA_DIRECT2D_SKIP_ADVANCED_BLEND` | `BOUNDED_SOURCE_COPY` and the Porter-Duff composite-mode pixels |

Setting either one never means "this runtime passed" for the withheld branch. Only the native
Windows gate (D2D-22) can close those rows.
