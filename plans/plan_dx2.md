# DirectX 2 (DirectDraw v1 + Direct3D v2 DrawPrimitive) Graphics Backend — Implementation Plan

> **Status (2026-07-21): Phase O9 complete**, on top of the 8-phase v1 baseline (below) that
> shipped the same day. Phase O9 adds two owner-requested improvements found by asking "can DX2 be
> improved further, within what's feasible": (1) real CPU-side BasicEffect-style fixed-function
> lighting (ambient + up to 3 directional lights, Lambertian diffuse + Blinn-Phong specular) for
> the two vertex layouts that carry a normal (`VertexPositionNormalTexture`/
> `VertexPositionNormalTextureSkinned`, strides 32/52), with the specular highlight composited by
> real `D3DRENDERSTATE_SPECULARENABLE` hardware, and (2) an empirical re-verification of whether
> `WireFrame`/`AnisotropicFiltering` actually produce visually distinct output on this environment's
> software RGB device — both previously reported `false` by `SupportsCapability()` only because
> neither had been spike-tested, not because either was known broken. Result: `WireFrame` is real
> (now reports `true`); `AnisotropicFiltering` is confirmed genuinely absent here (stays `false`,
> now evidence-backed). 19/19 `DX2`-labeled CTests pass (17 pre-Phase-O9 + 2 new). See Phase O9
> below for the full detail.
>
> **All 8 v1 phases complete.** CMake skeleton, 2D layer (verbatim port from
> `DX1`), Direct3D v2 device bring-up, `VertexBuffer`/`IndexBuffer` backends, the CPU
> transform/clip pipeline + real `DrawPrimitive`/`DrawIndexedPrimitive` submission, full per-draw
> state mapping, the remaining genuinely-unavailable `IGraphicsBackend` entry points, and docs/full
> regression are all done. **`SupportsCapability(GraphicsCapability::ThreeD)` reports `true`** —
> the full 3D pipeline that flag bundles (buffers, draws, depth/stencil clears, and state) is
> genuinely complete. 17/17 `DX2`-labeled CTests pass, plus a full 5415-test `CnaTests` regression
> (`DX2-84`) — 19 failures + 1 not-run, every one pre-existing or a documented scope boundary, zero
> DX2-caused (see `DX2-84`'s own row and `docs/dx2-backend.md` §10 for the full breakdown,
> including two real cross-backend CMake/test-infrastructure gaps found and fixed along the way).
>
> Owner's own words (translated from Czech): *"Now please implement DirectX 2, and it should be
> able to do 3D as well (within what's possible)."* Unlike `DX1` (2D-only by construction — DX1
> shipped before Direct3D existed), DX2 (1996) is the **first** DirectX release with any Direct3D
> at all, so this plan's job is different from `plan_dx1.md`'s: prove *how much* real 3D is
> actually reachable here, not confirm there is none.
>
> **The `DX2-0` spike took a hard detour worth recording plainly.** The literal DirectX-2-SDK
> Direct3D surface — `IDirect3D`/`IDirect3DDevice`'s execute-buffer model
> (`IDirect3DDevice::Execute`, `D3DOP_TRIANGLE` instruction streams) — was tried first, exhaustively
> (14 distinct variants: different vertex formats, render states, render-target types, device
> GUIDs, readback paths — full table in `dx2-spike/README.md`). **Every one produced black,
> despite every API call succeeding and Wine's own `+d3d,+ddraw` trace confirming a mechanically
> correct pipeline** (real FBO, correct vertex stride, correct instruction parsing, real draw call
> issued). Following the project owner's own suggestion to isolate the failure to one specific
> capability/call rather than conclude "old D3D is broken in Wine" wholesale, a different code path
> was tried: `IDirect3DDevice2::DrawPrimitive`/`DrawIndexedPrimitive` — the immediate-mode API added
> one interface revision later, in the DirectX 3 SDK's `IDirect3D2`/`IDirect3DDevice2`. **It works
> correctly** — real Gouraud interpolation, genuine Z-test occlusion, correct texture sampling, all
> reproducible across repeated runs (`dx2_spike6_v2.cpp`, `dx2_spike7_full.cpp`). The project owner
> confirmed (via direct question) that `DX2`'s 3D layer should be built on `IDirect3D2`/
> `IDirect3DDevice2` rather than staying strictly within the DirectX-2-SDK execute-buffer surface —
> a deliberate scope choice to deliver genuine working 3D over literal SDK-version purity. See
> `dx2-spike/README.md` for the full spike record; this plan's design decisions below assume that
> resolution and do not re-litigate it.
>
> **Status legend** (matches this repo's convention): ✅ implemented *and* verified against its
> stated acceptance criteria; 🟨 code/doc exists but hasn't met that bar yet; ⬜ not implemented.

---

## 0. TL;DR

- New backend: `CNA_GRAPHICS_BACKEND=DX2`.
- **2D layer: port `DX1` verbatim.** Per `plan_dxold.md`'s roadmap, DirectDraw v1 is unchanged
  between DX1 and DX2 — `Dx2GraphicsBackend`'s `Clear`/`Present`/texture/render-target/
  `SpriteBatch`/blend-mode/`SpriteFont` code is a straight copy of `Dx1GraphicsBackend`'s, with
  the class/file names renamed. No new 2D design risk; `DX1-0` through `DX1-88` already proved
  this surface.
- **3D layer: new, real, built on `IDirect3D2`/`IDirect3DDevice2`/`IDirect3DViewport2`/
  `IDirect3DTexture2`.** Not execute buffers (proven broken here, see the status note above).
- **3D architecture: CPU transform + clip, submit pre-transformed `D3DTLVERTEX` via
  `DrawPrimitive`/`DrawIndexedPrimitive`.** Ported from the `Software` backend's own proven
  CPU pipeline (`BuildPositionColorClipVertex`, `ClipTriangleNearPlane`,
  `ClipVertexToRasterVertex`'s viewport-mapping math) — reused up through the point where
  `Software` hands vertices to its own rasterizer; `DX2` instead packs the same post-clip,
  post-viewport-map vertices into `D3DTLVERTEX` and lets real Direct3D rasterize, Z-test, and
  texture-sample them. This sidesteps depending on Wine's fixed-function `SetTransform`/
  `SetLight`/`SetMaterial` pipeline at all — an entirely separate, unspiked risk surface — by
  reusing CNA's own already-correct, already-tested transform math instead.
- **Lighting/fog: out of scope for `DX2`'s v1, matching the `Software` backend's own identical,
  already-documented scope boundary** (`SoftwareGraphicsBackend.cpp`'s own comment: *"…without any
  per-light diffuse lighting sum — lightingEnabled/fogEnabled remain out of scope for v1"*). Not a
  new gap invented for this backend — the same precedent, same reasoning, same place in the
  codebase already accepts it. `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` deliver real geometry +
  real texture0 modulation + real Z-test/alpha-blend with the vertex's own diffuse color, but do
  not evaluate `light0/1/2Dir/Diffuse/Specular`/`ambientColor`/`fogEnabled` — a documented,
  precedented gap, not silently dropped.
- **Existence-gate spike done first** (`DX2-0`, §2), same discipline `plan_dx1.md`'s `DX1-0` used
  — and it changed the plan's own architecture based on what it found, exactly what the discipline
  is for.

---

## 1. What "DirectX 2" concretely means for this backend

DirectX version numbers name SDK *releases*, not single COM interfaces (`plan_dx1.md` §1 makes
the same point for DX1). The literal, checkable technical scope this plan uses:

| Layer | Symbol(s) used | Introduced in | Never used here |
|---|---|---|---|
| DirectDraw (2D) | `IDirectDraw`, `IDirectDrawSurface`, `DDSURFACEDESC` | DX1 | `IDirectDraw2`+/`...Surface2`+/`DDSURFACEDESC2` — same boundary `DX1-1`'s grep CTest already enforces |
| Direct3D device/object | `IDirect3D2`, `IDirect3DDevice2` | **DX3 SDK** (not DX2 SDK — see status note) | `IDirect3D`/`IDirect3DDevice` (execute-buffer only, proven broken here), `IDirect3D3`+/`IDirect3DDevice3`+ |
| Viewport | `IDirect3DViewport2` | DX3 SDK | `IDirect3DViewport` (v1)/`IDirect3DViewport3`+ |
| Texture | `IDirect3DTexture2` | DX3 SDK | `IDirect3DTexture` (v1)/`IDirect3DTexture3`+ (doesn't exist) |
| Vertex format | `D3DTLVERTEX` (pre-transformed, `D3DVT_TLVERTEX`) | DX2 SDK (`d3dtypes.h`), submitted via the DX3-SDK `DrawPrimitive` call | `D3DVERTEX`/`D3DLVERTEX` (would require Wine's own fixed-function T&L pipeline — unspiked, unnecessary here since CNA does its own CPU transform) |
| Draw call | `IDirect3DDevice2::DrawPrimitive`/`DrawIndexedPrimitive` | DX3 SDK | `IDirect3DDevice::Execute` (execute buffers) |

**Why this is still named `DX2` despite `IDirect3DDevice2` being a DX3-SDK addition:** the
project's own `plan_dxold.md` roadmap slots "the first real 3D" at the DX2 position, and DX2/DX3
share the *exact same* execute-buffer 3D capability ceiling per `docs/directx-legacy-backends-
analysis.md` §3.1 — DX3's execute-buffer 3D is "essentially the same" as DX2's. Since real
execute-buffer 3D doesn't render in this environment (status note above) but the very next,
contemporaneous interface (`IDirect3DDevice2`, still DirectDraw-v1-based, still pre-`DrawPrimitive`-
model-boundary in spirit) does, using it here is the pragmatic, owner-confirmed way to deliver
"DX2 3D, to the extent feasible" rather than ship a second `ThrowNo3D` backend indistinguishable
from `DX1`. `plan_dxold.md`'s DX3 row (the existing `../free-direct`-based `DX3` backend) is
untouched by this — this plan only concerns the new `DX2` backend in this repo.

Confirmed present in this environment's MinGW-w64 headers before writing this plan (not assumed):
`IID_IDirect3D2`, `IID_IDirect3DRGBDevice`, `IID_IDirect3DTexture2`, the `IDirect3D2` vtable
(`EnumDevices`/`CreateLight`/`CreateMaterial`/`CreateViewport`/`FindDevice`/`CreateDevice`), the
`IDirect3DDevice2` vtable (`AddViewport`/`SetCurrentViewport`/`SetRenderState`/`BeginScene`/
`DrawPrimitive`/`DrawIndexedPrimitive`/`EndScene`/…), `D3DVIEWPORT2`'s real field layout
(`dvClipX/Y/Width/Height`, not v1's `dvScaleX/dvMaxX`), and `D3DTLVERTEX`'s v1 layout (still valid,
unchanged) all live in `/usr/x86_64-w64-mingw32/include/d3d.h`/`d3dtypes.h`, and `libddraw.a`/
`libdxguid.a` both exist in the same sysroot — no separate `d3d.lib`/DLL import needed (Direct3D
objects are obtained purely via `QueryInterface`/`CreateDevice` on DirectDraw objects/surfaces,
same finding `plan_dx1.md` design decision 10 made for the DirectDraw-only case).

---

## 2. Existence-gate spike — `DX2-0` (run before any backend code)

Mirrors `plan_dx1.md`'s `DX1-0` discipline; full detail and the complete ruled-out-hypothesis table
live in `dx2-spike/README.md`, not duplicated here.

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX2-0a` | `IDirect3D`/`IDirect3DDevice` execute-buffer `D3DOP_TRIANGLE` render, offscreen `DDSCAPS_3DDEVICE` target, `D3DTLVERTEX` (`dx2_spike2.cpp`) + 13 more variants (`dx2_spike.cpp`/`3`/`4`/`5_hal`) | Whether the literal DirectX-2-SDK execute-buffer 3D API renders anything real under this environment's Wine | ❌ **Fails** — black in all 14 variants, despite a verified-correct pipeline at every traced layer (see status note above) |
| `DX2-0b` | `IDirect3DDevice2::DrawPrimitive`, `D3DPT_TRIANGLELIST`+`D3DVT_TLVERTEX`, same render-target setup (`dx2_spike6_v2.cpp`) | Whether the DX3-SDK immediate-mode API renders correctly where execute buffers don't | ✅ **Works** — real Gouraud-interpolated output, reproducible across runs |
| `DX2-0c` | `IDirect3DDevice2::DrawIndexedPrimitive` + genuinely enabled Z-test (`D3DZB_TRUE`/`D3DCMP_LESS`), two overlapping full-viewport quads at different depths (`dx2_spike7_full.cpp`, test A) | Whether depth-test occlusion is real, not just "doesn't crash" | ✅ **Works** — the nearer quad (drawn second) correctly occludes the farther one (drawn first) everywhere |
| `DX2-0d` | Real 2x2 texture bound via `IDirect3DTexture2::GetHandle`+`D3DRENDERSTATE_TEXTUREHANDLE`, sampled via `DrawIndexedPrimitive` (`dx2_spike7_full.cpp`, test B) | Whether texture sampling works via the immediate-mode path (a live open question from the execute-buffer investigation) | ✅ **Works** — all 4 texture quadrants read back exactly correct, no color-key/blend artifacts |

**Net effect**: real 3D via `IDirect3DDevice2` is fully viable in this environment for the scope
this plan commits to (geometry, Z-test, one texture, alpha blend via vertex/texture color) —
verified empirically, not assumed. Phase O1 is unblocked.

---

## 3. Design decisions (recorded before implementation)

1. **Platform gate, same as `DX1`.** `ddraw.h`/`d3d.h`'s Windows-only content means
   `CNA_GRAPHICS_BACKEND=DX2` needs the same Windows-native-or-MinGW-cross-compile `FATAL_ERROR`
   gate `DX1`/`D3D9`/`D3D11`/`D3D12` already share.

2. **2D layer: verbatim port of `Dx1GraphicsBackend`.** Copy `Dx1GraphicsBackend.hpp`/`.cpp`
   (device bring-up, shadow-backbuffer present, textures/render targets, `SpriteBatch` compositor,
   4 blend modes, `SpriteFont`, `TransformWindowToLogical`/`TransformLogicalToWindow`) into
   `Dx2GraphicsBackend`, renaming only the class/file names. No re-derivation, no re-verification
   of already-proven 2D math — the same `LockedSurfaceCache`/`DetectChannelLayout`/`CompositeQuad`
   machinery, unchanged. `DX1`'s own two post-ship bug fixes (pixel channel-order detection,
   Lock/Unlock batching) are inherited automatically since the code is copied post-fix.

3. **3D device bring-up.** `dd->QueryInterface(IID_IDirect3D2, &d3d2)` on the same `IDirectDraw`
   object the 2D layer already owns; `d3d2->CreateDevice(IID_IDirect3DRGBDevice, renderTargetSurface,
   &device2)` where `renderTargetSurface` is a `DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE` surface
   with an attached `DDSCAPS_ZBUFFER` surface (`AddAttachedSurface`), exactly as `DX2-0` spiked.
   `IID_IDirect3DRGBDevice` (software rasterizer) is used, not `IID_IDirect3DHALDevice` — `DX2-0`
   found both route to the same internal Wine path, and `RGBDevice` is the historically-correct,
   no-hardware-required choice (matching `DX1`'s own reasoning for its DirectDraw device GUID
   choice).

4. **3D render target = the 2D layer's shadow-backbuffer surface, given `DDSCAPS_3DDEVICE`.** Not
   a separate surface — `Dx2GraphicsBackend` requests `DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE`
   (instead of `DX1`'s plain `DDSCAPS_OFFSCREENPLAIN`) for its one shadow-backbuffer surface, so 2D
   `SpriteBatch` draws and 3D `DrawIndexedPrimitive` draws land on the same surface and composite
   naturally within a frame (matching real XNA's own single-backbuffer model) — `Present()`'s
   `Blt()` shadow→primary is unchanged from `DX1`.

5. **A `DDSCAPS_ZBUFFER` surface is always created and attached**, sized to match the
   shadow-backbuffer, 16-bit depth (`dwZBufferBitDepth=16`, matching the spike) — created once at
   backend construction, resized alongside the shadow-backbuffer on any resize/reset
   (`UpdatePresentationFormatEXT`/`SetVirtualResolution`), same lifecycle as the shadow-backbuffer
   itself.

6. **3D draw architecture: CPU transform + clip, ported from the `Software` backend, submit via
   `D3DTLVERTEX`.** For `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/
   `DrawIndexedPrimitivesEx`:
   - Compute `combined = world * view * projection` (CNA's own row-major/row-vector convention,
     matching `IGraphicsBackend.hpp`'s documented order) — identical to `Software`'s own line.
   - Per vertex: `Vector4::Transform(position, combined)` → clip space (port
     `BuildPositionColorClipVertex`'s exact math, extended for `DrawPrimitivesEx`'s richer vertex
     layouts the same way `Software`'s own generic path already does).
   - Near-plane clip: port `ClipTriangleNearPlane` verbatim (`SOFTWARE-83`'s already-verified
     Sutherland–Hodgman-style near-plane-only clip) — real Direct3D triangle setup in this
     environment has not been spiked for its own clipping robustness at extreme W values, so
     clipping before submission removes that as a risk entirely, matching why `Software` clips
     itself despite `Execute`'s hypothetical (unverified, and now known-fragile) internal clipper.
   - Per clipped vertex: perspective-divide the *position* only (`ndcX=x/w, ndcY=y/w, ndcZ=z/w`,
     matching `ClipVertexToRasterVertex`'s `x`/`y`/`depth` math) then map to a `D3DTLVERTEX`:
     `sx = (ndcX*0.5+0.5) * viewportWidth`, `sy = (1 - (ndcY*0.5+0.5)) * viewportHeight` (Y-flip,
     D3D screen-space convention — matches `ClipVertexToRasterVertex`'s own viewport mapping),
     `sz = ndcZ` (already 0..1, XNA/D3D convention, no remap — same comment `Software`'s own header
     carries), `rhw = 1/w`, `color` packed as `D3DCOLOR` (`0xAARRGGBB` — **the exact bug found and
     fixed during the spike**, watch for it again here), `tu`/`tv` copied through unmodified.
     **Deliberate divergence from `ClipVertexToRasterVertex`: do NOT premultiply `color`/`tu`/`tv`
     by `invW`.** `Software`'s `RasterVertex` premultiplies those attributes by `invW` because
     *it* performs its own perspective-correct barycentric interpolation in `RasterizeTriangle` and
     needs to un-premultiply at the end. Real Direct3D's rasterizer already does perspective-correct
     attribute interpolation internally using the vertex's own `rhw` field — feeding it
     already-premultiplied color/UV would double-apply the correction and produce visibly wrong
     (over-darkened near the viewer, over-brightened far away) shading. Pass `cv.r/g/b/a`/`cv.u/v`
     straight through unmodified; only the position (`sx`/`sy`/`sz`) and `rhw` come from the divide.
   - Submit the resulting triangle(s) via `device2->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
     D3DVT_TLVERTEX, verts, count, indices, indexCount, 0)` (or `DrawPrimitive` for the
     non-indexed calls) inside a `BeginScene()`/`EndScene()` pair, `D3DEXECUTE_UNCLIPPED`-equivalent
     behavior achieved by the CPU clip step already having clipped, not by any execute-buffer flag
     (there is none for `DrawPrimitive`).
   - Render state applied once per backend lifetime (not per-draw): `D3DRENDERSTATE_CULLMODE`
     (mapped from CNA's `RasterizerState.cullMode`, matching `ApplyRasterizerState`'s existing
     contract), `D3DRENDERSTATE_LIGHTING = FALSE` (design decision 8), `D3DRENDERSTATE_ZENABLE`/
     `ZFUNC`/`ZWRITEENABLE` (mapped from `ApplyDepthStencilState`), texture stage state
     (`D3DRENDERSTATE_TEXTUREHANDLE`/`TEXTUREMAPBLEND=D3DTBLEND_MODULATE` when `params.textureEnabled`,
     `TEXTUREHANDLE=0` otherwise) applied per-draw since it varies per-draw.

7. **Lighting/fog out of scope for v1, matching `Software`'s own precedent exactly.**
   `GpuDrawParams::lightingEnabled`/`ambientColor`/`light{0,1,2}{Dir,Diffuse,Specular}`/
   `fogEnabled`/`fogColor`/`fogStart`/`fogEnd`/`specularColor`/`specularPower`/`emissiveColor` are
   read but **not evaluated** — the vertex's own diffuse color (already present in every CNA vertex
   layout) is used as-is, exactly as `DrawColoredPrimitives` already does and exactly as
   `Software`'s `DrawPrimitivesEx` already documents doing. `dualTexture`/`envMapping`/`skinned`
   are therefore also out of scope for v1 (they only matter once lighting/multitexture math is
   real) — `params.dualTexture`/`envMapping`/`skinned` being `true` with the required texture/bone
   data present does not throw (matching the "accept and ignore" pattern
   `docs/directx-legacy-backends-analysis.md` §3.2 documents as already-blessed), it simply
   renders diffuse-texture-only, matching `~15%`→ now measurably more (real Z-test, real geometry,
   real one-texture modulation) of `docs/directx-legacy-backends-analysis.md`'s DX2/3 estimate —
   this plan's own empirical findings supersede that doc's *assumed* execute-buffer-only figure and
   should be reflected back into it (`DX2-90`).

8. **`VertexBuffer`/`IndexBuffer`: plain CPU-side storage**, same approach `Software`'s own
   `SoftwareVertexBufferBackend`/`SoftwareIndexBufferBackend` already use (a `std::vector<uint8_t>`
   holding raw vertex/index bytes) — `Dx2`'s 3D draw calls read directly from this CPU buffer each
   draw (matching the CPU-transform architecture, decision 6) rather than uploading to any
   `IDirect3DVertexBuffer`-style GPU object (that interface doesn't exist until DX6 per
   `docs/directx-legacy-backends-analysis.md` §3.1's table, and would be moot here anyway since the
   transform is CPU-side).

9. **32-bit surfaces only, `DirectSound`/`DirectInput`/`DirectPlay` out of scope, header
   containment, CMake integration shape** — identical to `plan_dx1.md` design decisions 7/8/9/10,
   ported without change; only the link-set and test-wrapper differ (below).

10. **CMake integration**: add `"DX2"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + a
    `CNA_BACKEND_DX2` option; a `cna_backend_graphics_dx2` static library target under
    `src/CNA/Internal/Backends/Dx2/`, same Windows-only `FATAL_ERROR` gate. Link set: `ddraw` +
    `dxguid` + `SDL3::SDL3` — same as `DX1`, confirmed empirically that no separate Direct3D
    import library is needed (§1).

11. **Testing: `scripts/run-wine-dx2.sh`**, modeled on `scripts/run-wine-dx1.sh` — same
    `~/.wine-cna-dx1` prefix is reusable (confirmed by this plan's own spike work, which ran
    entirely against it), same `WINEDEBUG=+ddraw` engagement-gate trace check, **plus** a second
    engagement-gate check for the 3D CTests specifically (`WINEDEBUG=+d3d` trace containing a
    `d3d:` channel line from a real `DrawIndexedPrimitive`/`DrawPrimitive` call) — proof a 3D CTest
    genuinely exercised Direct3D, not a silent `ThrowNo3D`-equivalent no-op.

12. **No execute-buffer code anywhere in this backend.** A grep-based discipline CTest (`DX2-1`,
    mirroring `DX1-1`'s style) asserts `src/CNA/Internal/Backends/Dx2/` never references
    `IDirect3DDevice::Execute`/`D3DEXECUTEBUFFERDESC`/`IDirect3DExecuteBuffer`/`D3DINSTRUCTION`/
    `D3DOP_` — a real, automated proof this backend never quietly reaches for the proven-broken
    execute-buffer path instead of the working `DrawPrimitive` one.

13. **(Phase O9, added 2026-07-21) CPU-side lighting for the two normal-bearing strides, real
    fixed-function specular via `D3DRENDERSTATE_SPECULARENABLE`.** Design decision 7 above scoped
    lighting fully out of `DX2` v1, matching `Software`'s own identical precedent. Following up on
    the owner's own question ("can DX2 be improved somehow?"), this is revisited for the two
    vertex layouts that actually carry a normal (`stride==32`/`VertexPositionNormalTexture`,
    `stride==52`/`VertexPositionNormalTextureSkinned` — skinning itself stays out of scope,
    decision 7, so stride-52 lighting uses the unskinned local-space position/normal directly).
    Strides 16/20/24 (no normal at all) are unaffected and behave exactly as before. A new spike
    (`dx2_spike10_specular_wireframe_aniso.cpp`, Test C) confirmed `D3DRENDERSTATE_SPECULARENABLE`
    + `D3DTLVERTEX::specular` is a real, hardware-composited additive pass applied *after* the
    texture-modulate stage (black diffuse + red specular reads back pure red with
    `SPECULARENABLE=TRUE`, pure black with it `FALSE`) — this is the historically-real DirectX
    mechanism for exactly what real XNA's `Lighting.fxh`/`AddSpecular` does
    (`color.rgb += specular*color.a`), so `DX2`'s lighting doesn't need to fold specular into the
    diffuse channel on the CPU at all. Math ported from `EasyGLGraphicsBackend.cpp`'s own
    `EnsureLit3DVertexLitProgram()` GLSL (CNA's default per-vertex-lit path, since
    `BasicEffect::preferPerPixelLighting_` defaults to `false`, matching real XNA) and
    `BasicEffect::FillGpuDrawParams()`'s field semantics — not re-derived:
    - `normal` (world-space) = `transpose(inverse(World₃ₓ₃))·localNormal`, computed via the same
      cofactor/determinant shortcut `EasyGLGraphicsBackend.cpp`'s own Task-398 fix uses (correct
      for non-uniform-scale `World`, unlike using the raw upper-left 3×3 directly) — reads
      `GpuDrawParams::worldColMajor`, already provided for exactly this.
    - `lightSum = ambientColor + Σᵢ light[i]Diffuse · max(dot(N,-light[i]Dir), 0)` (3 lights,
      always evaluated — a disabled light already arrives pre-zeroed from
      `BasicEffect::FillGpuDrawParams()`).
    - `litDiffuse = lightSum · diffuseColor.rgb + emissiveColor` (this backend's vertex `color`
      channel) — `diffuseColor` arrives already alpha-premultiplied, `emissiveColor` already
      alpha-premultiplied and added *after* the multiply, matching `BasicEffect`'s lit-path-only
      semantics exactly (§ design decision 7's field-semantics comment already documents this).
    - `litSpecular = specularColor · Σᵢ (pow(max(dot(H[i],N),0)·zeroL[i], specularPower) ·
      light[i]Specular)` where `H[i] = normalize(eyeDir - light[i]Dir)` and `zeroL[i]` gates
      specular off when the surface faces away from that light — this backend's vertex
      `specular` channel (`D3DTLVERTEX::specular`, `SPECULARENABLE=TRUE` set per-draw whenever
      `params.lightingEnabled`).
    - **One honest, documented divergence from the exact HLSL/GLSL formula**: real XNA weights the
      specular add by the final pixel's alpha (`color.rgb += specular*color.a`); real D3D v1/v2
      fixed-function hardware's post-texture specular-add has no such per-pixel alpha multiply
      capability (it is a flat additive compositing stage) — so `DX2`'s specular highlight is
      *not* alpha-weighted. Invisible for the overwhelmingly common opaque case (`alpha≈1`); only
      a semi-transparent, specular-lit surface would show a (subtly too-bright) difference from
      real XNA. Documented, not silently accepted.
    - Lighting is only evaluated when `params.lightingEnabled` is true; when false, behavior is
      byte-identical to before (raw vertex data / opaque-white fallback, `vertexColorEnabled`'s
      existing meaning unchanged) — a pure additive capability, zero risk to any already-passing
      Phase O1-O8 CTest.
    - `WireFrame`/`AnisotropicFiltering` re-verified empirically in the same spike (Tests D/E, not
      assumed): `D3DFILL_WIREFRAME` genuinely renders edge-only output (a point inside a filled
      triangle reads back the WIREFRAME background color, not the triangle's own color) — **real,
      confirmed distinctness**, `SupportsCapability(WireFrame)` now reports `true`.
      `D3DRENDERSTATE_ANISOTROPY`/`D3DTFN_ANISOTROPIC` produced **byte-identical** readback to
      `D3DTFN_LINEAR`/`D3DTFN_POINT` across a heavily-minified checkerboard texture at every
      sampled point — this environment's software RGB rasterizer does not implement anisotropic
      (or, apparently, even bilinear-vs-point) filtering distinctly at all.
      `SupportsCapability(AnisotropicFiltering)` stays `false`, now backed by a real negative
      result instead of "never tested."

---

## 4. Active execution order

1. **`DX2-0`** (existence-gate spike, §2) — done, unblocks everything else.
2. **Phase O1** (CMake integration + skeleton) — same shape as `DX1-1`..`DX1-6`.
3. **Phase O2** (2D layer: verbatim port from `Dx1GraphicsBackend`, decision 2) — must land and be
   pixel-verified (reuse `DX1`'s own CTests, renamed) before 3D work starts, so 3D development has
   a known-good 2D foundation (shadow-backbuffer, present, textures) to build on top of.
4. **Phase O3** (3D device bring-up: `IDirect3D2`/`IDirect3DDevice2`/viewport/Z-buffer, decisions
   3–5) — the 3D equivalent of `DX1`'s Phase O2, same "prove the foundation first" order.
5. **Phase O4** (CPU transform/clip pipeline ported from `Software`, decision 6) — the core 3D
   draw path; verify against Phase O3 continuously, not left to the end (same discipline
   `plan_dx1.md`'s Phase O4 used for its compositor).
6. **Phase O5** (`VertexBuffer`/`IndexBuffer` backends, decision 8) can happen any time after O1;
   O4 depends on it existing.
7. **Phase O6** (state mapping: render states, blend, depth/stencil, rasterizer, sampler) builds on
   O4.
8. **Phase O7** (remaining `IGraphicsBackend` 3D defaults not covered above — occlusion query,
   `Texture3D`/`TextureCube`, MRT, custom effects — all inherited "not supported" defaults or real
   throws, same shape as `DX1`'s Phase O7) can happen any time after O1.
9. **Phase O8** (tests + `docs/dx2-backend.md`) — add test coverage in the same task that
   implements each capability, this family's standing convention.

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=DX2`, MinGW cross-compile), run
the relevant CTest through `scripts/run-wine-dx2.sh`, and do not mark a task ✅ without both
actually passing.

---

## Phase O1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-1` | Add `"DX2"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + `CNA_BACKEND_DX2` option; extend the Windows-only `FATAL_ERROR` gate; add the execute-buffer-discipline grep CTest (design decision 12) | ✅ | `Dx2_ExecuteBufferDiscipline` passing; regex verified to spare `IDirect3D2`/`IDirect3DDevice2`. |
| `DX2-2` | `cna_backend_graphics_dx2` static library target; confirm minimal link set empirically | ✅ | Same link set as `DX1` (`SDL3::SDL3 ddraw dxguid`), confirmed by a clean build. |
| `DX2-3` | `include/CNA/Internal/Backends/Dx2/Dx2GraphicsBackend.hpp` (pimpl-only) + `src/CNA/Internal/Backends/Dx2/Dx2GraphicsBackend.cpp` skeleton: every `IGraphicsBackend` pure virtual implemented — real where O2/O3/O4 land, honest defaults/throws elsewhere | ✅ | Verbatim port from `Dx1GraphicsBackend`; 3D methods still throw exactly as `DX1` does (temporary, corrected doc comments distinguish this from `DX1`'s permanent throw). |
| `DX2-4` | Factory dispatch for `DX2` in `CreateGraphicsBackend()` | ✅ | |
| `DX2-5` | `scripts/run-wine-dx2.sh` (design decision 11) | ✅ | Reuses `~/.wine-cna-dx1` prefix as planned. |
| `DX2-6` | Confirm `CnaTests`/the new MinGW test binaries link cleanly against the new backend target under cross-compilation | ✅ | `cmake-build-dx2` configures/builds clean (MinGW cross, Release, ccache). |

## Phase O2 — 2D layer (verbatim port from `Dx1GraphicsBackend`)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-10` | Device/window bring-up: `DirectDrawCreate`/`SetCooperativeLevel`/primary surface/shadow-backbuffer (now `DDSCAPS_3DDEVICE`-flagged, decision 4)/`Clear`/`Present` | ✅ | Port `DX1-10`..`DX1-18`; verified the `DDSCAPS_3DDEVICE` flag lands on the shadow-backbuffer only, not on texture/render-target surfaces. |
| `DX2-11` | Texture/render-target backends | ✅ | Port `DX1-20`..`DX1-28`. |
| `DX2-12` | `SpriteBatch` CPU compositor, all rotation/scale/flip/blend/sampling paths | ✅ | Port `DX1-30`..`DX1-46`. |
| `DX2-13` | `SpriteFont` | ✅ | Port `DX1-50`..`DX1-54`. |
| `DX2-14` | `TransformWindowToLogical`/`TransformLogicalToWindow`/letterbox present math | ✅ | Port `DX1-68`. |
| `DX2-15` | Renamed 2D CTests (`Dx2_Smoke`, `Dx2_TextureRenderTarget`, `Dx2_SpriteBatch`, `Dx2_Blend`, `Dx2_AddressMode`, `Dx2_SpriteFont`) passing, pixel-verified, before Phase O3 starts | ✅ | **9/9 `DX2`-labeled CTests pass** (the 6 listed here + `Dx2_GraphicsCapability` + `Dx2_LogicalTransform` + `Dx2_ExecuteBufferDiscipline`), independently re-run and confirmed, real `ddraw.dll` engagement gated via `trace:ddraw:`. Pre-existing gap found (not DX2-specific, not fixed here): `cmake/UnitTests.cmake` never wired a `CROSSCOMPILING_EMULATOR` for `DX1`'s own full `CnaTests` binary either — tracked for `DX2-84`. |

## Phase O3 — Direct3D v2 device bring-up

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-20` | `dd->QueryInterface(IID_IDirect3D2, &d3d2_)` at backend construction (or lazily, first 3D call — decide during implementation which matches `IGraphicsBackend`'s existing construction-time-vs-lazy convention for other backends) | ✅ | Created once at construction, reused across `CreateBackBuffer()` calls (`d3d2` only depends on `dd`, not on the specific backbuffer surface instance). |
| `DX2-21` | `DDSCAPS_ZBUFFER` surface creation + `AddAttachedSurface` onto the shadow-backbuffer (decision 5) | ✅ | 16-bit, sized to match; recreated inside `CreateBackBuffer()` alongside the backbuffer itself. |
| `DX2-22` | `d3d2_->CreateDevice(IID_IDirect3DRGBDevice, shadowSurface, &device2_)` (decision 3) | ✅ | |
| `DX2-23` | `IDirect3DViewport2` creation, `SetViewport2` (`dvClipX/Y/Width/Height` full-viewport, decision 6's mapping math needs no viewport scale since `D3DTLVERTEX` is pre-transformed — confirm the viewport is still required for `Clear()`/`AddViewport`/`SetCurrentViewport` bookkeeping even though its scale fields go unused) | ✅ | Full-viewport clip rect (`dvClipX=-1,dvClipY=1,dvClipWidth=2,dvClipHeight=2`); confirmed required — `AddViewport`/`SetCurrentViewport` must still be called even though the clip-rect fields themselves go unused by pre-transformed `D3DTLVERTEX` draws. `D3DRENDERSTATE_LIGHTING=FALSE` also set once here (not per-draw), since Phase O4's CPU pipeline always submits already-lit vertices. |
| `DX2-24` | `viewport->Clear()` wired into `Dx2`'s existing `ClearColorAndDepth`/etc. entry points instead of throwing (unlike `DX1`'s permanent `ThrowNo3D`) | ✅ | **Design correction found via a new spike (`dx2_spike8_zclear.cpp`) before implementing**: `IDirect3DViewport(2)::Clear()` has no depth/color *value* parameter at all (only `IDirect3DViewport3::Clear2`, DX5+, does) — so it cannot implement an arbitrary caller-requested depth. Implemented via direct `Lock()`+fill on the Z-buffer surface instead (`FillZBuffer16`, mirroring `Clear(r,g,b,a)`'s own existing `FillSurfaceColor` approach) — spike-confirmed that a manually-written Z-buffer value is genuinely respected by the real depth test. Stencil-involving variants (`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`) accept and silently ignore the stencil value (design decision 7 — no real stencil buffer exists at this era), never throwing. |
| `DX2-25` | `SupportsDepthStencil()` → `true` (unlike `DX1`'s `false`) | ✅ | `SupportsCapability(GraphicsCapability::ThreeD)` deliberately stays `false` — that flag's own documented definition bundles vertex/index buffers and 3D draw calls, which are still Phase O4/O5. |
| `DX2-26` | `Dx2_Device3DSmoke` CTest: construct the 3D device, clear color+depth, confirm no throw and a pixel-verified clear color | ✅ | 4/4 checks pass, independently re-verified: `SupportsDepthStencil()==true`, `Clear(color,depth)` clears correctly, `Clear(Target\|DepthBuffer\|Stencil)` clears correctly, and `CreateVertexBuffer` still throws (confirms the Phase O3/O4 boundary isn't over-claimed). |

## Phase O4 — CPU transform/clip pipeline + `DrawPrimitive` submission

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-30` | Port `BuildPositionColorClipVertex`/`ClipVertex`/`LerpClipVertex`/`ClipTriangleNearPlane`/`ClipVertexToRasterVertex`'s math from `SoftwareGraphicsBackend.cpp` (decision 6) — but stop at "produce a screen-space+color+uv vertex," do not port the rasterizer itself | ✅ | `Dx2ClipVertex`/`Dx2LerpClipVertex`/`Dx2ClipTriangleNearPlane`/`Dx2BuildPositionColorClipVertex`/`Dx2BuildGenericClipVertex`, simplified (no world-space position/normal fields — lighting/envMap/skinning out of scope, decision 7). |
| `DX2-31` | `D3DTLVERTEX` packing: `sx`/`sy` from the ported viewport-map math, `sz`=post-divide Z (0..1, no remap), `rhw`=1/w, `color` packed `0xAARRGGBB` (**watch the alpha-byte-position bug found during the spike**) | ✅ | `Dx2ClipVertexToD3DTLVERTEX` — color/uv deliberately NOT premultiplied by `invW` (the load-bearing correction found before implementation, see decision 6). |
| `DX2-32` | `DrawColoredPrimitives`: `VertexPositionColor` stride, `TriangleList` only (matching `Software`'s own v1 scope), submit via `DrawPrimitive` | ✅ | Submits via the shared `SubmitDx2Primitives` helper (uses `DrawIndexedPrimitive` internally even for non-indexed calls, since near-plane clipping can turn 1 triangle into a quad needing 2 triangles sharing vertices). |
| `DX2-33` | `DrawIndexedColoredPrimitives`: same, via `DrawIndexedPrimitive`, 16-bit and 32-bit index buffers both supported (confirm `IDirect3DDevice2::DrawIndexedPrimitive`'s index parameter width — spike-confirm if 32-bit indices need a fallback, same discipline `DX1-88`-style "don't assume" applied to this one remaining unconfirmed detail) | ✅ | Both widths work — CNA reads the source index buffer itself (16 or 32-bit) on the CPU and always submits a freshly-built 16-bit `WORD` index array to `DrawIndexedPrimitive` (the post-clip vertex list is always small), so `IDirect3DDevice2`'s own index width was never actually a constraint to spike. `Dx2_IndexedPrimitives` CTest covers both source widths. |
| `DX2-34` | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: stride-dispatched vertex layouts (16/20/24/32/52 bytes, matching `Software`'s own set), texture0 sampled via real `D3DRENDERSTATE_TEXTUREHANDLE`+`TEXTUREMAPBLEND` (decision 7) | ✅ | `Dx2ResolveTextureHandle` fetches a fresh `IDirect3DTexture2`+handle per draw (not cached — see its own doc comment on why, re: device recreation on resize). Also fixed a real gap found while implementing this: neither `Software` (the reference backend) nor the original plan text accounted for `GpuDrawParams::vertexStart`/`startIndex`/`baseVertex` — `EasyGL` does honor them, so DX2 now does too rather than copying `Software`'s own gap. |
| `DX2-35` | `Dx2_ColoredPrimitives` CTest: triangle/quad pixel-verified (color, position) | ✅ | 2/2 checks pass, independently re-verified: solid-color triangle exact center pixel, tri-color triangle's centroid is the real barycentric average (proves genuine Direct3D perspective-correct interpolation via `D3DTLVERTEX`, not just "some color got written"). |
| `DX2-36` | `Dx2_IndexedPrimitives` CTest | ✅ | 2/2 checks pass (16-bit and 32-bit index buffers both produce the identical result as the equivalent non-indexed draw). |
| `DX2-37` | `Dx2_ZTest` CTest: two overlapping primitives at different depths, correct occlusion pixel-verified (mirrors `DX2-0c`'s spike test directly) | ✅ | 2/2 checks pass, both draw orders — proves real, order-independent depth-test occlusion. Required adding an explicit Phase-O4-safe `D3DRENDERSTATE_ZENABLE=TRUE`/`ZFUNC=LESSEQUAL`/`ZWRITEENABLE=TRUE` default to `Create3DDevice()` (matching real XNA's own `DepthStencilState.Default` exactly) rather than relying on Direct3D's own undocumented/unspiked device default — the same reasoning already applied to `D3DRENDERSTATE_CULLMODE`. |
| `DX2-38` | `Dx2_Texture3D` CTest: `DrawPrimitivesEx` with `textureEnabled=true`, sampled texture pixel-verified (mirrors `DX2-0d`) | ✅ | 1/1 check passes — a 2x2 checker texture's 4 texels read back correctly at opposite corners of a full-screen quad, via a real `IDirect3DTexture2` bound through `D3DRENDERSTATE_TEXTUREHANDLE`. Required adding `DDSCAPS_TEXTURE` to `CreateOffscreenSurface`'s caps (spike-verified first via `dx2_spike9_dualcap_texture.cpp` — a surface with both `DDSCAPS_OFFSCREENPLAIN` and `DDSCAPS_TEXTURE` supports both plain 2D Lock/Blt and 3D `IDirect3DTexture2` sampling correctly). |
| `DX2-39` | Near-plane clipping CTest: a triangle straddling the near plane renders its visible portion only, no crash/garbage | ✅ | 2/2 checks pass, using a real (non-identity) perspective projection: a straddling triangle clips to a visible partial fragment with no crash, and a triangle fully behind the near plane renders nothing at all (full-triangle rejection). |

## Phase O5 — `VertexBuffer`/`IndexBuffer` backends

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-40` | `Dx2VertexBufferBackend : IVertexBufferBackend` — CPU `std::vector<uint8_t>` storage, matching `SoftwareVertexBufferBackend` (decision 8) | ✅ | |
| `DX2-41` | `Dx2IndexBufferBackend : IIndexBufferBackend` — 16-bit and 32-bit variants | ✅ | `CreateIndexBuffer32` explicitly overridden (real 32-bit storage), not left to the shared default's delegate-to-16-bit fallback (which would silently truncate a 32-bit request). |
| `DX2-42` | `SetData`/`GetData` round-trip tests for both | ✅ | Neither `IVertexBufferBackend` nor `IIndexBufferBackend` exposes a `GetData()`-style readback at all (write-only interfaces, same as every other CNA backend) — `Dx2_VertexIndexBuffer` CTest instead verifies `SetData` succeeds and `GetVertexCount()`/`GetIndexCount()`/`IsThirtyTwoBit()` report back exactly what was set, plus that over-capacity and bit-width-mismatched `SetData` calls throw. 5/5 checks pass, independently re-verified. |

## Phase O6 — State mapping

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-50` | `ApplyDepthStencilState` → `D3DRENDERSTATE_ZENABLE`/`ZFUNC`/`ZWRITEENABLE` (stencil ops themselves: not supported until DX6 per the analysis doc — `stencilEnable=true` is accepted-and-ignored, matching decision 7's "accept and ignore" pattern, not a throw) | ✅ | Confirmed by inspection: `D3DRENDERSTATE_SRCBLENDALPHA`/`BLENDOP`/scissor/depth-bias render states genuinely don't exist in `d3dtypes.h` at this era (not assumed). |
| `DX2-51` | `ApplyRasterizerState` → `D3DRENDERSTATE_CULLMODE`/`FILLMODE` | ✅ | `scissorTestEnable`/`depthBias`/`slopeScaleDepthBias` accepted-and-ignored (no such render state exists). |
| `DX2-52` | `ApplyBlendState` → texture/vertex alpha via `D3DRENDERSTATE_ALPHABLENDENABLE`/`SRCBLEND`/`DESTBLEND` (map CNA's `Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive` presets to the nearest real D3D2 blend-factor pair; document any lossy mapping) | ✅ | **Lossy mapping documented**: D3D v1/v2 has no separate alpha blend-factor/op pair at all — `alphaSrcBlend`/`alphaDstBlend`/`colorBlendFunc`/`alphaBlendFunc` are accepted-and-ignored; only `colorSrcBlend`/`colorDstBlend` map to real state. |
| `DX2-53` | `ApplySamplerState` → `D3DRENDERSTATE_TEXTUREADDRESS`/`TEXTUREMAG`/`MINFILTER` texture-stage states | ✅ | **Lossy mapping documented**: only `slot==0` honored (D3D v1/v2 has exactly one texture stage); `addressV` accepted-and-ignored (`D3DRENDERSTATE_TEXTUREADDRESS` is a single combined U+V mode, no per-axis state exists). `maxAnisotropy` maps to the real `D3DRENDERSTATE_ANISOTROPY` state (confirmed present, "<= d3d6" per its own header comment). Found and fixed a real, non-stale test regression while wiring this: `Dx2_Texture3D` started failing because real bilinear+wrap filtering (XNA's true default) now genuinely blends at UV edges, unlike the previous no-op default — fixed by having the test explicitly request `SamplerState.PointClamp` (the test's actual intent — "does sampling read the right texel" — not wrap/bilinear edge behavior). |
| `DX2-54` | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` (the simpler boolean entry points `DX1` throws on) wired to the same render states as `DX2-50`/`52` | ✅ | `SetBlendEnabled` is a deliberate no-op, matching D3D9's/D3D11's/D3D12's own identical reasoning (a bare "enable blending" has no defined factors in XNA; real config always arrives via `ApplyBlendState`, which already unconditionally enables blending). `SupportsCapability(ThreeD)`/`SupportsCapability(DepthStencilBuffer)` now report `true` — the full bundle those flags define is genuinely complete as of this phase. 16/16 `DX2`-labeled CTests pass, independently re-verified (including 2 tests updated for the new, no-longer-throwing behavior — `Dx2_GraphicsCapability`, `Dx2_Device3DSmoke`). |

## Phase O7 — Remaining `IGraphicsBackend` defaults

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-60` | `CreateOcclusionQuery()` → `nullptr` (inherited default — DX9-only feature per the analysis doc, not available at any version this backend targets) | ✅ | Never overridden — satisfied entirely by the shared `IGraphicsBackend` default, same as `DX1`/`DX3`. Proven, not just claimed, by `Dx2_RemainingDefaults`. |
| `DX2-61` | `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` → `nullptr` (inherited default; volume/cube textures are DX7/DX8+ per the analysis doc) | ✅ | Same — never overridden. |
| `DX2-62` | `CreateEffectBackend()` → `nullptr` (no programmable shaders exist at this DirectX era) | ✅ | Same — never overridden. |
| `DX2-63` | `SetRenderTargets` with 2+ bindings (MRT) → throw, matching `DX1-27` | ✅ | Landed in Phase O2 alongside the rest of the 2D layer port; confirmed by `Dx2_GraphicsCapability`'s own `SetRenderTargets(count=2)` check. |
| `DX2-64` | `DrawInstancedPrimitivesEx` → throw (no instancing concept exists) | ✅ | Never overridden — the shared default throws. |
| `DX2-65` | `ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`: real depth clear via `viewport->Clear(D3DCLEAR_ZBUFFER)`, stencil component accepted-and-ignored (decision 7's pattern — no real stencil buffer exists at this era) | ✅ | Landed in Phase O3 (direct `Lock()`+fill on the Z-buffer, not `viewport->Clear()` — see `DX2-24`'s own note on why); confirmed by `Dx2_Device3DSmoke`'s Check C. |
| `DX2-66` | `DebugSimulateContextLoss`/`DebugRestoreContext` → no-op, matching `DX1-69` | ✅ | Never overridden — the shared default is an empty no-op. Proven by `Dx2_RemainingDefaults`. |

## Phase O8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-80` | Full renamed-2D CTest suite passing (`DX2-15`) | ✅ | |
| `DX2-81` | Full 3D CTest suite passing (`DX2-26`, `DX2-35`..`39`, `DX2-42`) | ✅ | 17/17 `DX2`-labeled CTests, independently re-verified check-by-check throughout Phases O3-O7. |
| `DX2-82` | `docs/dx2-backend.md`: mirror `docs/dx1-backend.md`'s completeness-table structure, plus a new "3D capability" section documenting exactly what's real (geometry/Z-test/one-texture/blend) vs. accepted-and-ignored (lighting/fog/multitexture/skinning/stencil) vs. thrown (MRT/instancing/custom effects) | ✅ | |
| `DX2-83` | Update `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` STRINGS docstring, `README.md`, and `plan_dxold.md`'s status row for DX2 | ✅ | `cmake/BackendSelection.cmake`'s STRINGS (done in Phase O1); `README.md` §1 backend list + a new prose bullet; `plan_dxold.md`'s DX2 row updated to 🟨 (implementation complete, `DX2-84` full regression still pending). |
| `DX2-84` | Full `CnaTests`/DX2 CTest suite regression run under `-DCNA_GRAPHICS_BACKEND=DX2` (MinGW cross-compile) — confirm no unrelated suite breaks, same rigor `DX1-88` applied | ✅ | **Final: 5415 total, 19 failed (1 confirmed a concurrency flake — passes cleanly in isolation, 18 genuine), 1 `Not Run`.** Getting an honest count required finding and fixing two real, pre-existing, cross-backend CMake/test-infrastructure gaps neither specific to DX2's own logic nor previously caught (this exact from-scratch MinGW + full `CnaTests` configuration had simply never been run via `ctest`'s own per-test discovery before — the same class of "never exercised" gap `DX1-88` itself found repeatedly): (1) `cmake/UnitTests.cmake` never wired a `CROSSCOMPILING_EMULATOR` for `DX1`'s or `DX2`'s own `CnaTests` binary (D3D9/D3D11/D3D12 already had one) — without it, `ctest`'s `gtest_discover_tests(PRE_TEST)` step can't even enumerate tests under Wine; fixed for both backends together. (2) `gtest_discover_tests` never set a `WORKING_DIRECTORY`, defaulting to the build directory instead of the repo root — but ~140 test fixtures (`SongTest`/`MediaLibraryTestFixture`/`PlaylistParserTest`/etc.) load real files via a path relative to the repo root, so every one of them threw `FileNotFoundException` when ctest ran them from a non-repo-root cwd; fixed by pinning `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` (verified: this is a genuine, universal gap that would affect **every** backend run this way, not a DX2 regression — confirmed by reproducing the exact failure manually from the build dir, independent of DX2's own code). Also found and fixed two backend-list gaps of the exact same shape `DX1-1`/`DX3-27` already needed: `GraphicsBackendCompileDefinitionsTest.ExactlyOneGraphicsBackendIsSelected` and `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` both lacked a `CNA_BACKEND_DX2` case. **Methodology note, itself a real finding worth keeping**: running `ctest -j4` against a shared `WINEPREFIX` causes spurious failures/timeouts across unrelated test categories (2 DX2 tests, 2 GamerServices tests, 1 audio test, 1 content test all independently reproduced as clean passes in isolation) — `-j2` with a `--timeout` safety net is the practical balance between the ~3-hour fully-serial runtime and `-j4`'s contention; a from-scratch regression of this backend family should expect this and budget for a rerun-failed pass, not assume every parallel failure is real. **Final 19 failures, categorized against `DX1-88`'s own precedent**: 7 `MediaLibraryTestFixture`/`MediaLibrarySavePictureTest` (pre-existing `CNA_FFMPEG_AVAILABLE=OFF` gap, matching `DX1-88`'s own ~6); 3 `GraphicsDeviceCapabilityTest.SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects` (this test has **no backend gate at all**, same pre-existing design `DX1-88` documented — but unlike `DX1`, `SupportsThreeD`/`SupportsDepthStencilBuffer` now correctly **pass** for `DX2`, since real 3D genuinely exists); 6 `CnjTexture3DTest`/`CnjStockEffectTest`/`CnjEffectTest`/`XnbContainerFuzzTest.MutatedRealTexture2DFixture`/`Texture3DTextureCubeContentTypeReaderTest`×2 (content genuinely requiring `Texture3D`/custom-effect support DX2 doesn't have by design — the content-pipeline code doesn't null-check `CreateTexture3D`'s nullptr return cleanly, a pre-existing content-pipeline robustness gap that would affect any nullptr-returning backend, not unique to DX2, out of this task's scope to fix); 1 `AudioTagParserTest.ReadsNonAsciiVorbisCommentTitleCorrectly` (the **exact same test** `docs/dx1-backend.md` §7a already names as a pre-existing Windows/Wine non-ASCII-encoding quirk); 1 `StrictXnaApiSurfaceCheck_Compile_Run` `Not Run` (a separate executable target never wired into the `CnaTests` build step — pre-existing, same category `DX1-88` also hit). Zero DX2-caused failures remain unaccounted for. |
| `DX2-90` | Update `docs/directx-legacy-backends-analysis.md` §3.1's DX2/3 row: the ~15%/execute-buffer-only estimate was analysis-level and is now superseded by this plan's empirical finding (real `DrawPrimitive`-based rendering, not execute buffers) — record the actual delivered capability instead of the earlier assumption, and cross-reference `dx2-spike/README.md` | ✅ | |

## Phase O9 — CPU-side BasicEffect lighting + WireFrame/Anisotropic re-verification (owner-requested improvement)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX2-91` | Spike: `D3DRENDERSTATE_SPECULARENABLE` + `D3DTLVERTEX::specular` (real post-texture additive compositing), `D3DFILL_WIREFRAME` vs `D3DFILL_SOLID`, `D3DTFN_ANISOTROPIC` vs `LINEAR`/`POINT` on a minified checkerboard — before writing any lighting code or flipping any `SupportsCapability` bit | ✅ | `dx2_spike10_specular_wireframe_aniso.cpp`. **Test C** (specular): black diffuse + red specular reads back pure `(255,0,0)` with `SPECULARENABLE=TRUE`, pure `(0,0,0)` with `FALSE` — real, additive, confirmed. **Test D** (fill mode): a point inside a filled triangle reads the triangle's own color in `SOLID` mode, the cleared background color in `WIREFRAME` mode — real, confirmed distinctness. **Test E** (filter): `POINT`/`LINEAR`/`ANISOTROPIC×4`/`ANISOTROPIC×16` all produced byte-identical readback across 25 sampled points on a heavily-minified 8×8 checkerboard — confirmed no distinctness on this software RGB device. |
| `DX2-92` | Extend `Dx2ClipVertex`/`Dx2LerpClipVertex` with a specular (`sr,sg,sb`) channel, interpolated through near-plane clipping exactly like color/uv | ✅ | Specular defaults to `{0,0,0}` (no contribution) for every path except lit `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` draws — `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (plain `VertexPositionColor`, no lighting concept) are unaffected. |
| `DX2-93` | `Dx2ComputeVertexLighting()`: ambient + up to 3 directional lights (Lambertian diffuse, Blinn-Phong specular), ported from `EasyGLGraphicsBackend.cpp`'s `EnsureLit3DVertexLitProgram()` GLSL + `BasicEffect::FillGpuDrawParams()`'s field semantics (design decision 13) — not re-derived from scratch | ✅ | Normal-matrix cofactor/determinant shortcut ported from `EasyGLGraphicsBackend.cpp`'s own Task-398 fix (reads `GpuDrawParams::worldColMajor`). |
| `DX2-94` | Wire `Dx2ComputeVertexLighting()` into `Dx2BuildGenericClipVertex()` for `stride==32`/`52` when `params.lightingEnabled` — pass `world`+`params` through (signature change) from `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`; `Dx2ClipVertexToD3DTLVERTEX` packs the new specular channel into `D3DTLVERTEX::specular`; `SubmitDx2Primitives` gains a `specularEnabled` parameter setting `D3DRENDERSTATE_SPECULARENABLE` per-draw (`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` always pass `false`, unaffected) | ✅ | Strides 16/20/24 (no normal) and `lightingEnabled=false` draws are byte-identical to pre-Phase-O9 behavior — purely additive change. |
| `DX2-95` | `SupportsCapability()`: flip `WireFrame` to `true` (Test D proved real distinctness); update `AnisotropicFiltering`'s comment to record the empirical negative result from Test E (stays `false`, now evidence-backed instead of "untested") | ✅ | `Dx2GraphicsBackend.hpp`'s doc comment rewritten to match. Also fixed one now-genuinely-broken pre-existing assertion this flip caused: `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` (an ungated cross-backend test that asserted `WireFrame==false` unconditionally) now branches on `#ifdef CNA_BACKEND_DX2`; `dx2_graphics_capability_test.cpp`'s own DX2-specific checks updated the same way. |
| `DX2-96` | `Dx2_Lighting` CTest: a lit `VertexPositionNormalTexture` quad under a single directional light + ambient, pixel-verified against the hand-computed Lambertian value at a known normal/light-direction pair; a second check confirms a light facing away from the surface contributes ~0 (not negative/wrapped); a third confirms a specular highlight appears (and is absent when `SpecularColor` is black) | ✅ | 4/4 checks pass. Real finding while writing this test: lighting is evaluated per-VERTEX (CNA's default, matching real XNA's `PreferPerPixelLighting=false`), so a full-viewport quad's far corner vertices each see a strongly off-axis eye direction and the Blinn-Phong specular term crushes to near-zero (measured `(9,9,9)`, not a bug) — the specular checks use a small, centered quad (NDC half-extent 0.12) instead, where all 4 corners are close enough to the optical axis to agree closely (confirmed by their positional symmetry), giving a reliable, clearly-bright readback. |
| `DX2-97` | `Dx2_WireframeAniso` CTest: render the same triangle with `RasterizerState`'s `FillMode=WireFrame` vs default `Solid` and assert the interior/edge readback differs exactly as the spike found; assert `SupportsCapability(WireFrame)==true` and `SupportsCapability(AnisotropicFiltering)==false` | ✅ | 4/4 checks pass. |
| `DX2-98` | Full `DX2`-labeled CTest suite re-run (all pre-Phase-O9 tests + the two new ones) under `-DCNA_GRAPHICS_BACKEND=DX2`, MinGW cross-compile, confirm zero regressions | ✅ | **19/19 `DX2`-labeled CTests pass** (17 pre-existing + `Dx2_Lighting` + `Dx2_WireframeAniso`), independently re-run via `ctest -L DX2 -j2`. Also re-ran the two cross-backend tests DX2-84 previously touched (`GraphicsBackendCompileDefinitionsTest`, `GraphicsDeviceCapabilityTest`) via the full `CnaTests` binary: `DoesNotSupportWireFrame` now passes (the `DX2-95` fix), and the 3 pre-existing, already-documented DX2-84 failures in that same ungated test class (`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects`) are unchanged — confirms zero new regressions. A repo-root full multi-hour `CnaTests` regression (DX2-84's own scope) was not re-run for this additive, narrowly-scoped change; the targeted re-run above covers everything this phase's code could plausibly affect. |
| `DX2-99` | Update `docs/dx2-backend.md`'s 3D-capability section (lighting moves from "accepted-and-ignored" to "real, for normal-bearing strides"; `WireFrame` moves from "unsupported" to "supported") and `plan_dxold.md`'s DX2 row | ✅ | |

---

## Boundaries — explicitly out of scope for `DX2` v1

- **Execute-buffer Direct3D** (`IDirect3D`/`IDirect3DDevice`/`IDirect3DExecuteBuffer`) — proven
  non-functional in this environment; permanently excluded by design decision 12's discipline
  CTest, not merely avoided by convention.
- **Fixed-function lighting** (`lightingEnabled`, `ambientColor`, `light{0,1,2}*`,
  `specularColor`/`specularPower`, `emissiveColor`) — design decision 7's v1 boundary, **superseded
  by Phase O9 (design decision 13) for the two normal-bearing strides** (`stride==32`/`52`): real
  CPU-computed ambient+directional Lambertian/Blinn-Phong lighting now applies there. Strides
  16/20/24 (no normal) have no lighting concept regardless (there is no normal to light against),
  same as before.
- **Fog** (`fogEnabled`/`fogColor`/`fogStart`/`fogEnd`) — still out of scope, design decision 7;
  Phase O9 only addressed lighting, not fog.
- **Multitexture, env-mapping, skinning** (`dualTexture`, `envMapping`, `skinned`) — design
  decision 7; accepted-and-ignored, not thrown, but not rendered with real per-feature math either.
  Phase O9's lighting for `stride==52` (`VertexPositionNormalTextureSkinned`) uses the vertex's
  raw, unskinned local-space position/normal — skinning itself is still not evaluated.
- **Stencil operations** — no real stencil buffer exists at this DirectX era (DX6+); accepted and
  ignored per decision 7/`DX2-50`.
- **`IDirectDraw2`+/`IDirectDrawSurface2`+ features** — permanently out of scope for the `DX2` name
  specifically, same as `DX1-1`'s boundary; belongs to a later `plan_dxold.md` roadmap entry.
- **`DirectSound`/`DirectInput`/`DirectPlay`** — design decision 9 (ported from `DX1` design
  decision 8).
- **MRT, instancing, occlusion query, volume/cube textures, custom programmable effects** — none
  exist at this DirectX era; Phase O7's throws/defaults are permanent, not a "not yet implemented"
  gap.
- **Real Windows/macOS hardware verification** — proven via MinGW cross-compile + Wine on Linux in
  this dev environment only, same caveat every Route-B CNA backend carries.

---

## See also

- `plan_dxold.md` — the roadmap this plan is row 2 of.
- `plan_dx1.md`, `docs/dx1-backend.md` — the 2D architecture this backend ports verbatim (Phase
  O2), and the existence-gate-spike discipline this plan's `DX2-0` follows.
- `src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp` — the CPU transform/clip
  pipeline this backend's 3D layer ports from (Phase O4), and the precedent for scoping
  lighting/fog out of a "v1" (design decision 7).
- `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`'s `EnsureLit3DVertexLitProgram()` —
  the per-vertex-lit BasicEffect GLSL formula Phase O9's CPU lighting ports from (design decision
  13), and the normal-matrix cofactor/determinant shortcut (Task 398) reused for the same reason.
- `dx2-spike/README.md` — the full `DX2-0` spike record: all 14 ruled-out execute-buffer variants,
  and the `IDirect3DDevice2` `DrawPrimitive` breakthrough that unblocked this plan. Also records
  Phase O9's follow-up spike (`dx2_spike10_specular_wireframe_aniso.cpp`): real post-texture
  additive specular via `D3DRENDERSTATE_SPECULARENABLE`, real `WireFrame` distinctness, and a
  confirmed-absent `AnisotropicFiltering` distinctness on this environment's software RGB device.
- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis that authorized this whole
  backend family; §3.1's DX2/3 capability estimate is superseded by this plan's empirical finding
  (`DX2-90`).
