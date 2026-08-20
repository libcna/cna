# Direct2D 1.1 renderer

`CNA_GRAPHICS_RENDERER=DIRECT2D` is CNA's Windows-only, SpriteBatch-oriented 2D renderer. It is
graphics-renderer identity 39 and never falls back to another renderer. CMake rejects the selection
deterministically on non-Windows targets.

Application drawing is issued through `ID2D1DeviceContext`. A BGRA-capable D3D11 device and a DXGI
1.2 flip-sequential swap chain exist only to create the Direct2D device/surfaces and present an
HWND. They are not an application-visible D3D11 renderer. The renderer does not use DirectWrite,
WIC, SDL_Renderer, or a hidden 3D/compositing pass. Use `CNA_GRAPHICS_RENDERER=D3D11` for 3D.

## Supported 2D contract

- The only texture, render-target, and backbuffer format is XNA `SurfaceFormat::Color`. Public RGBA
  bytes are converted byte-exactly to Direct2D's native `DXGI_FORMAT_B8G8R8A8_UNORM` storage and
  back on readback. Odd widths, padded row pitches, asymmetric RGB channels, nontrivial alpha,
  repeated updates, and short-pitch rejection have independent tests.
- `Texture2D` supports level zero and explicitly authored mip levels. `SetData`, `GetData`, device
  recovery, and SpriteBatch sampling use the selected authored level. Mip-linear filter families
  interpolate between two initialized authored levels; an incomplete chain falls back toward the
  nearest initialized level instead of sampling undefined data.
- `RenderTarget2D` supports one color target at level zero, rendering, full/partial readback,
  level-zero upload, sampling after unbind, all three `RenderTargetUsage` values, and transparent
  reallocation after recovery. Mipmapped render targets are deliberately unsupported and fail at
  construction. The former generated-mip path was removed because its NPOT downsample omitted
  source quadrants (D2D-78).
- SpriteBatch supports crop, origin, positive or negative source rectangles, horizontal/vertical
  flips, rotation, nonuniform scale, batch transforms, viewport transforms, scissor clipping,
  `Clamp`/`Wrap`/`Mirror`, and all nine `TextureFilter` values within the authored-Texture2D
  contract. Unknown filter, address, presentation, or `SpriteEffects` values and non-finite
  transforms fail before native state changes.
- `Opaque`, `AlphaBlend`, and `NonPremultiplied` are supported. Exact symmetric Porter-Duff tuples
  map to Direct2D image-composite modes. Blend tuples without an exact Direct2D representation,
  channel masks, coverage masks, and non-white blend factors are rejected transactionally, with
  the previously accepted blend remaining active. In particular,
  `BlendState::Additive` is XNA `SourceAlpha/One`; it is not Direct2D `One/One`. Because the renderer
  cannot implement that contract for every source type, `AdditiveBlending` is reported false and
  Additive is rejected consistently rather than approximated.
- A render target used as a SpriteBatch source stays GPU-resident. Native Direct2D built-in
  `ColorMatrix`/`Premultiply` effects provide supported tint/straight-alpha decoration when the
  runtime exposes them. A runtime that reports the effects unregistered receives a named
  `NotSupportedException` for that exact decorated path; unexpected or device-loss HRESULTs are
  never converted into a compatibility skip.
- Sampling the currently bound render target is rejected as a read/write alias. `Present` while a
  render target remains bound is also rejected; callers must unbind explicitly. `SetData` during
  active drawing commits outstanding commands first, so an Immediate-mode draw keeps the old
  bitmap snapshot and the next draw observes the successful update.
- The logical framebuffer implements Letterbox, Overscan, Stretch, NativeBackBuffer, and
  FixedHeightDynamicWidth. Backbuffer readback remains in logical coordinates. Empty-frame
  `Present` still observes an SDL client resize. Final presentation uses linear Direct2D
  interpolation.
- CNA sprite, viewport, and scissor coordinates are logical-framebuffer pixel units, not Windows
  DIPs. The Direct2D context and every bitmap are forced to 96 DPI so one Direct2D unit equals one
  logical target pixel. `NativeBackBuffer` is 1:1 with client pixels; the other presentation modes
  map the logical framebuffer into the client-pixel swap chain. SDL3's Win32 HWND property supplies
  the native target. Deterministic conversion/resize tests run under Wine; physical multi-monitor
  DPI and desktop-capture validation remain an external Windows gate.
- `SpriteSortMode::Immediate` issues each sprite during `Draw`. Present interval zero calls the
  flip-model swap chain with `Present(0, 0)`; CNA does not advertise a tearing capability or add
  `DXGI_PRESENT_ALLOW_TEARING`. Intervals one and two use the corresponding synchronized interval.
- Registered ordinary textures recover from their RGBA shadows. Render targets are recreated
  transparent. Device loss is classified consistently across Direct2D, D3D11, and DXGI HRESULTs;
  public lost/resetting/reset events fire once in order. Unregistered stale resources fail instead
  of being used on a new device generation.

## DPI and monitor changes

A DPI change -- the user changing a monitor's scaling, or the window moving to a differently scaled
monitor -- reaches this renderer as a change of the client rectangle in physical pixels, which the
per-frame size check already acts on: the swap chain is resized and the backbuffer target and
logical framebuffer are rebuilt at the new size, without a restart.

Nothing else has to react, and that is a consequence of D2D-54 rather than an omission. Every
Direct2D surface is pinned at 96 DPI and every CNA coordinate is a physical client pixel, so there
is no DIP-derived quantity to recompute when the scale changes. The logical/window round trip is
derived from the backbuffer's own pixel size, so it follows the resize automatically.

What the renderer does do is *observe* the scale: with `CNA_DIRECT2D_DIAGNOSTICS=1` a change of
SDL's window-units-to-pixels ratio is logged with both sizes. A run that claims presentation
evidence therefore records the DPI it actually ran at, instead of leaving it to be assumed.
Physical multi-monitor and non-96-DPI validation remains an external Windows gate.

## Alpha representation at every boundary

Direct2D composes premultiplied colors. CNA's public surface does not, so the exact place where a
straight-alpha value becomes premultiplied matters. Every boundary is listed here; there is no
other conversion site in the renderer.

| Boundary | Representation | Conversion |
|---|---|---|
| `Texture2D::SetData` / `ImageData` bytes | Exactly the bytes the caller passed, RGBA order | None. Only the channel order changes (RGBA to native BGRA) |
| `Texture2D`, `RenderTarget2D`, logical framebuffer storage | `DXGI_FORMAT_B8G8R8A8_UNORM`, `D2D1_ALPHA_MODE_PREMULTIPLIED` | None; the stored bytes are taken as already premultiplied |
| `BlendState::AlphaBlend` and `BlendState::Opaque` sources | Premultiplied, as stored | None |
| `BlendState::NonPremultiplied` sources | Straight, as stored | Premultiplied exactly once, by the Direct2D `Premultiply` effect on the GPU path or by `MakeSpritePixels` on the CPU fallback |
| SpriteBatch `Color` modulation | Straight per channel | `Color.RGB` scales the (already premultiplied) RGB, `Color.A` scales only alpha. It is never applied twice |
| `ColorMatrix` tint stage | Premultiplied by default; `D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT` for `NonPremultiplied` and for `Opaque` | The straight mode keeps `Color.A` from attenuating the copied RGB, whose source factor is `One` |
| `GraphicsDevice::Clear` | Straight `D2D1::ColorF` | Direct2D premultiplies internally |
| Physical swap-chain surface | `D2D1_ALPHA_MODE_IGNORE`, `DXGI_ALPHA_MODE_IGNORE` | None. It is the presentation destination only; application readback never reads it |
| `GetData` / `GetBackBufferData` | Premultiplied bytes of the read surface, RGBA order | None. Only the channel order changes (native BGRA to RGBA) |
| Readback staging bitmap | Inherits the source bitmap's pixel format | None |

Two consequences are worth stating explicitly. Readback returns *premultiplied* bytes even for
content uploaded as straight alpha under `NonPremultiplied`, because the conversion happened on the
way in and is not undone. And no application-visible bitmap uses an alpha-ignoring format: the only
alpha-ignoring surface in the renderer is the physical backbuffer, which nothing reads.

## Capability boundary

The capability query is an exhaustive switch over all 13 current `GraphicsCapability` values:

| Capability | Direct2D |
|---|---:|
| `ThreeD` | false |
| `DepthStencilBuffer` | false |
| `MultiSampleAntiAliasing` | false |
| `MultipleRenderTargets` | false |
| `AnisotropicFiltering` | true |
| `WireFrame` | false |
| `OcclusionQuery` | false |
| `CustomEffects` | false |
| `Texture3D` | false |
| `MultiStreamVertexInput` | false |
| `Instancing` | false |
| `StencilBuffer` | false |
| `AdditiveBlending` | false |

The fixed depth format is `DepthFormat::None`; no real depth or stencil buffer exists. A
multisample request other than the non-multisampled `0`/`1` convention is rejected. Multiple render
targets, vertex/index buffers, ordinary/indexed/instanced 3D draws, cube/volume textures and
targets, queries, wireframe, depth bias, custom effects, and 3D clear/state operations are outside
this renderer. The policy-aware 3D entry points throw by default and return only inert safe handles
when `Unsupported3DGraphicsCallBehavior::WarnAndStub` was explicitly selected.

## Validation gates

The Direct2D CTest label contains five sequential tests:

- `Direct2D_Smoke`: HWND, D3D11 staging readback, and point SpriteBatch draw.
- `Direct2D_2DParity`: public pixel, transform, update, render-target, presentation, recovery,
  capability, and deterministic-rejection oracles.
- `Direct2D_Lifetime`: repeated target switching, readback, recovery, resize, and resource churn.
- `Direct2D_Unit`: the `Direct2D*` GoogleTest subset from `CnaTests`, run through the dedicated
  Direct2D Wine/Proton runner and prefix.
- `Direct2D_Soak`: per-branch SpriteBatch throughput and a long resize/recovery/target-switch soak
  with a process working-set oracle. The throughput numbers are reported, not gated, unless
  `CNA_DIRECT2D_BENCH_BASELINE_MS` supplies a baseline recorded on comparable hardware --
  a timing threshold on shared CI, on WARP, or under Wine is either too loose to prove anything or
  fails at random. `CNA_DIRECT2D_SOAK_CYCLES` raises the default 2000 cycles for an acceptance run.

A bounded cross-build and run uses:

```bash
cmake --build cmake-build-direct2d-integration --parallel 2 \
  --target CnaTests cna_test_direct2d_smoke cna_test_direct2d_2d_parity \
  cna_test_direct2d_lifetime cna_test_direct2d_soak
scripts/run-direct2d-virtual-display.sh \
  ctest --test-dir cmake-build-direct2d-integration -L Direct2D -V
```

### Virtual display, Wine prefix, and Proton runtime

Direct2D always presents to a real HWND, so every run needs a display.
`scripts/run-direct2d-virtual-display.sh` is the single canonical decision: an isolated Xvfb at an
explicit geometry (`CNA_DIRECT2D_XVFB_SCREEN`, default `1280x800x24`), a private X authority file,
and cleanup on every exit path. It is reentrant, so a `ctest` already launched under it is reused
instead of nesting a second server, and both `run-wine-direct2d.sh` and `run-proton-direct2d.sh`
enter it automatically. `CNA_DIRECT2D_USE_HOST_DISPLAY=1` opts out for interactive debugging.

`scripts/run-direct2d-fresh-wine-suite.sh <build-dir>` runs the whole label against a prefix created
from nothing (`wineboot --init` in a temporary directory, removed afterwards unless
`CNA_DIRECT2D_KEEP_PREFIX=1`), so a pass cannot depend on a developer's accumulated `~/.wine` state.
The prefix deliberately receives no winetricks package, no DLL override, and no DXVK install: the
supported contract is Wine's own built-in `d2d1`/`d3d11`/`dxgi`.

The Proton lane is pinned by `scripts/direct2d-proton-pin.txt`, a priority-ordered list of Steam
library directory names. `Proton - Experimental` is refused unless
`CNA_DIRECT2D_PROTON_ALLOW_EXPERIMENTAL=1` is set for a one-off investigation, because Steam
upgrades it underneath a passing run and such a result cannot be reproduced. Every run publishes the
resolved runtime identity — Proton directory, build id, bundled Wine build, and DXVK version — to
stderr and, with `CNA_DIRECT2D_PROTON_IDENTITY_FILE`, to an artifact file.
`CNA_DIRECT2D_PROTON_DRY_RUN=1` resolves and publishes that identity without running anything.

`scripts/verify-direct2d-parallel-jobs.sh` rejects missing, nonnumeric, or greater-than-two build
parallelism in the Direct2D workflow/helpers. `CNA_ENABLE_NET=OFF` excludes only tests belonging to
the omitted Net and GamerServices modules; it retains the Direct2D unit subset.

`scripts/verify-direct2d-debug-log.py` turns the native debug-layer output into a gate rather than
an artifact. With `CNA_DIRECT2D_DEBUG_LAYER=1` the renderer emits every stored D3D11 message as
`[Direct2D D3D11 debug] severity=<S> category=<C> id=<I> <description>` and closes the report with
`[Direct2D diagnostics] live-object report end.`. The parser fails on a message of severity `ERROR`
or `CORRUPTION`, on a live object outside the whitelist of objects CNA itself still holds when it
reports (`ID3D11Device`, `ID3D11DeviceContext`, `ID3D11Debug`, `ID3D11InfoQueue`), on a run whose
diagnostics or debug layer never engaged, and on a truncated report. Its own classification is
regression-tested against the committed positive/negative fixtures in `tests/fixtures/direct2d/`:

```bash
python3 scripts/verify-direct2d-debug-log.py --self-test
python3 scripts/verify-direct2d-debug-log.py build/direct2d-native-diagnostics/*.log
```

`scripts/direct2d_mutation_check.py` proves the suite can actually go red. It introduces one
deliberate defect at a time -- a blend-factor mapping, the Porter-Duff coefficients, the letterbox
scale, the mip rounding, a dropped validation -- and requires the named test to fail. `--dry-run`
only checks that each anchor still matches exactly once (the part that goes stale after a refactor)
and needs no build; `--run <build-dir>` performs the full apply, rebuild, and expect-failure pass.

`scripts/validate_direct2d_plan.py` keeps [`plans/plan_direct2d.md`](../plans/plan_direct2d.md) honest about
its own state. It fails on a status outside the documented set, a duplicated task, a `✅` row citing
no evidence at all, a cited path/CTest name/`CNA_DIRECT2D_*` variable that does not exist, a
reference to a task without a row, an incomplete row missing from the plan's closing classification
table, and a stale `direct2d-plan-status` summary marker. Run it after any status change:

```bash
python3 scripts/validate_direct2d_plan.py
```

Two further documents complete the picture:
[`direct2d-runtime-support-matrix.md`](direct2d-runtime-support-matrix.md) gives every
runtime-dependent branch one of four dispositions (supported, fallback, named reject, open task)
across Windows, WARP, Wine, and Proton, and
[`direct2d-release-gate.md`](direct2d-release-gate.md) defines the release criteria and the command
that evaluates them.

Wine 10.0 is useful evidence for the supported portable/native-API surface but is not physical
Windows. WineD3D does not register Direct2D's built-in ColorMatrix effect and does not implement
the bounded-copy image composite used by the Opaque pixel oracle. The compatibility run therefore
sets only the narrow `CNA_DIRECT2D_SKIP_RENDER_TARGET_DECORATION` and
`CNA_DIRECT2D_SKIP_ADVANCED_BLEND` branches. Those skips do not claim native coverage.

The manual Windows graphics workflow is prepared to run MSVC with the Direct2D/D3D11 debug layers,
hardware and WARP lifetime passes, diagnostics, and live-object output. Native built-in-effect and
composite pixels, physical display/DPI/presentation capture, adapter-specific behavior, and
debug-layer/live-object acceptance remain external evidence limits until a real x64 Windows run is
recorded. See [`plans/plan_direct2d.md`](../plans/plan_direct2d.md) for that evidence backlog and nonblocking
performance/process work.
