# DX7 (real `IDirectDraw7` + `IDirect3D7`, flattened device model) Backend — Completeness Status

`DX7` is architecturally `DX6` (`docs/dx6-backend.md`) with a real structural change: genuinely new
`IDirectDraw7`/`IDirectDrawSurface7` and `IDirect3D7`/`IDirect3DDevice7` interfaces (created via the
new `DirectDrawCreateEx` entry point), the complete removal of the viewport-object concept this
backend family carried since `DX2-0` (`IDirect3DDevice7::SetViewport`/`Clear` are direct device
methods instead), a shorter `CreateDevice` signature, and a much simpler texture-binding mechanism
(`SetTexture(stage, surface)` directly, no handle indirection at all). Stencil is unchanged from
`DX6`, ported verbatim.

**This document only covers what's specific to `DX7`.** Every capability row, boundary, and test
result in `docs/dx6-backend.md` (and, further back, `docs/dx5-backend.md`/`docs/dx3-backend.md`/
`docs/dx2-backend.md`) applies identically here (same 2D compositor, same 3D pipeline, same
lighting math, same known permanent limitations) — refer to those for the full completeness table.
This document covers: the `DX7-0` spike, the real code deltas vs. `DX6`, and the DX7-specific
CTest/regression results.

---

## 1. Existence-gate spike (`DX7-0`)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX7-0a1` | `DirectDrawCreate` (v1) + `QueryInterface(IID_IDirectDraw7)` | Whether the OLD upgrade chain still works despite `ddraw.h`'s "cannot derive" note | ✅ Works |
| `DX7-0a2` | `DirectDrawCreateEx(nullptr, &dd7, IID_IDirectDraw7, nullptr)` | Whether the NEW DX7 entry point works | ✅ Works |
| `DX7-0b` | Combined depth+stencil Z-buffer surface (same shape `DX6-0` proved) via `IDirectDrawSurface7` | Whether the v7 surface layer supports the same stencil-capable Z-buffer | ✅ Works |
| `DX7-0c` | `IDirect3D7::CreateDevice(IID_IDirect3DRGBDevice, surface7, &device7)` — no trailing outer param | Whether device creation works with the new (shorter) signature | ✅ Works |
| `DX7-0d` | `device7->SetViewport(&D3DVIEWPORT7{...})` — no viewport object created at all | Whether the flattened, no-viewport-object model works | ✅ Works |
| `DX7-0e` | `device7->Clear(...)` called directly, then a stencil WRITE pass (left-half quad) | Whether device-direct `Clear` + stencil write survive the API flattening | ✅ Works — left half green, right half black |
| `DX7-0f` | A full-screen quad with `STENCILFUNC=EQUAL` on top of `DX7-0e`'s result | Whether stencil TEST still correctly gates per-pixel | ✅ Works — left half red, right half correctly rejected |
| `DX7-0g` | `device7->SetTexture(0, texSurface7)` — direct surface bind, no handle at all — then a textured quad | Whether the new no-handle texture-binding mechanism actually samples correctly | ✅ Works — exact solid-red readback |

Every readback matched its predicted value exactly, on the first run — no spike-authoring bugs at
all this round. See `dx7-spike/README.md` for the full record.

## 2. Real code deltas vs. `DX6`

- **`IDirectDraw7`/`IDirectDrawSurface7`, created via `DirectDrawCreateEx`** — a genuinely new entry
  point (`ddraw.h` documents `IDirectDraw7` as "not interchangeable with earlier DirectDraw
  interfaces"), used directly rather than the old `DirectDrawCreate`+`QueryInterface` upgrade chain
  every `DX2`..`DX6` backend used (spike-confirmed both paths empirically work, `DX7-0a1`/`DX7-0a2`
  — this backend uses the new, correct-for-this-era one).
- **The whole `IDirect3DViewport3` object is REMOVED.** `IDirect3D7` has no `CreateViewport` method
  at all. `IDirect3DDevice7::SetViewport(D3DVIEWPORT7*)` and `IDirect3DDevice7::Clear(...)` are
  direct device methods instead — no `CreateViewport`/`AddViewport`/`SetCurrentViewport`/
  `DeleteViewport`/`Clear2` calls exist anywhere in this backend. `D3DVIEWPORT7` is also a simpler
  struct than `D3DVIEWPORT2` (no `dvClipX`/`dvClipY`/`dvClipWidth`/`dvClipHeight` fields — just
  `dwX`/`dwY`/`dwWidth`/`dwHeight`/`dvMinZ`/`dvMaxZ`).
- **`IDirect3D7::CreateDevice` drops the trailing `IUnknown* outer` parameter** `DX5`/`DX6`'s
  `IDirect3D3::CreateDevice` needed (present on `DX5`/`DX6`, absent on `DX2`/`DX3`, absent again on
  `DX7` — a real signature oscillation across this backend family's own history).
- **Texture binding is a direct `IDirect3DDevice7::SetTexture(stage, surface)` call**, replacing the
  whole `D3DRENDERSTATE_TEXTUREHANDLE` + `IDirect3DTexture2::GetHandle` +
  `QueryInterface(IID_IDirect3DDevice2)` dance `DX5`'s own design decision 6 needed and `DX6`
  inherited unchanged — no `IDirect3DTexture2` view is needed at all, no per-draw handle-resolution
  helper exists in this backend.
- **Real finding, NOT anticipated by the `DX7-0` spike** (which tested texture *binding* but not
  texture *blending*): Wine's `IDirect3DDevice7::SetRenderState` outright REJECTS the legacy
  `D3DRENDERSTATE_TEXTUREMAPBLEND` render state every prior backend in this family used ("Render
  state 0x15 is invalid in d3d7"), discovered when the first full CTest pass threw at runtime. Fixed
  with `SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE)` — the real DX7-native
  texture-stage mechanism, confirmed correct (not just "doesn't throw") by `Dx7_Texture3D`'s own
  pixel-verified sampling check.
- **Stencil (`D3DRENDERSTATE_STENCILENABLE`/`FUNC`/`FAIL`/`ZFAIL`/`PASS`/`REF`/`MASK`/`WRITEMASK`
  against the same combined depth+stencil Z-buffer surface `DX6` introduced) is unchanged, ported
  verbatim** — spike-confirmed (`DX7-0e`/`DX7-0f`) and CTest-confirmed (`Dx7_Stencil`) it survives
  both the viewport removal and the texture-binding change.
- **Hardware T&L is real in this environment's Wine** (`EnumDevices7` reports a genuine
  `IID_IDirect3DTnLHalDevice`, `"Wine D3D7 T&L HAL"`) **but deliberately NOT adopted** — this whole
  backend family submits CPU-pre-transformed-and-lit `D3DTLVERTEX` vertices by design, matching
  XNA/FNA's own CPU-side `BasicEffect` math exactly; real hardware T&L would require submitting
  un-transformed vertices and delegating to the device's own fixed-function pipeline, the opposite
  architecture. `IID_IDirect3DRGBDevice` (the same software device class every prior backend uses)
  is used here too, for consistency.
- **Cube environment maps stay deliberately deferred** (same class of reason as `DX6`'s
  multitexture deferral): genuine cube-map sampling needs a 3-component texture coordinate
  (`D3DFVF_TEXCOORDSIZE3`) and a `DDSCAPS2_CUBEMAP` surface, both requiring a second vertex layout
  and extending the whole CPU pipeline — disproportionate scope, deferred with the reason recorded
  rather than silently dropped.
- Everything else — 2D layer, CPU transform/clip pipeline, `D3DFVF_TLVERTEX` submission, Phase O9
  lighting, `VertexBuffer`/`IndexBuffer` backends, remaining `IGraphicsBackend` defaults — is an
  unmodified port of `DX6`'s own code, with types renamed to v7.

## 3. CTest results

**20/20 `DX7`-labeled CTests pass**, all green after fixing the `D3DRENDERSTATE_TEXTUREMAPBLEND`
finding above (the only compile/runtime issue this port hit — zero *compile* errors at all, since
DX7 changes no struct layout the compiler would catch):

`Dx7_ExecuteBufferDiscipline`, `Dx7_Smoke`, `Dx7_TextureRenderTarget`, `Dx7_SpriteBatch`,
`Dx7_Blend`, `Dx7_AddressMode`, `Dx7_SpriteFont`, `Dx7_GraphicsCapability`,
`Dx7_LogicalTransform`, `Dx7_Device3DSmoke`, `Dx7_VertexIndexBuffer`, `Dx7_ColoredPrimitives`,
`Dx7_IndexedPrimitives`, `Dx7_ZTest`, `Dx7_Texture3D`, `Dx7_Clipping`, `Dx7_RemainingDefaults`,
`Dx7_Lighting`, `Dx7_WireframeAniso`, and the renamed **`Dx7_Stencil`** (real stencil
write-then-test through the full XNA public API — `GraphicsDevice.DepthStencilState`, proving
stencil survives both the viewport removal and the texture-binding change; 4/4 checks pass:
stamp-left/stamp-right/test-left-passes/test-right-rejected).

`SupportsCapability(GraphicsCapability::WireFrame)` reports `true` (inherited from `DX6`'s own
finding); `AnisotropicFiltering` stays `false` (same confirmed-absent finding).
`SupportsCapability(GraphicsCapability::DepthStencilBuffer)` reports `true`.

Targeted cross-backend regression (mirroring `DX6-84`'s own scope decision):
`GraphicsBackendCompileDefinitionsTest.ExactlyOneGraphicsBackendIsSelected`,
`GraphicsDeviceValidationTest.SetRenderTargets_*`, and `GraphicsDeviceCapabilityTest.*` all pass
except the same 3 pre-existing, already-documented ungated-test-class failures `DX2-84` found
(`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects`) — zero new
regressions.

The execute-buffer discipline check (`scripts/check-dx7-execute-buffer-discipline.sh`) is
substantially stricter than `DX6`'s own: it also forbids any pre-v7 DirectDraw/Direct3D interface
(`IDirectDraw2-6`/`IDirectDrawSurface1-6`/`IDirect3D3`/`IDirect3DDevice3`), the entire removed
viewport object (`IDirect3DViewport*`/`CreateViewport`/`AddViewport`/`SetCurrentViewport`/
`DeleteViewport`/`Clear2`), the old texture-handle mechanism (`D3DRENDERSTATE_TEXTUREHANDLE`/
`IDirect3DTexture2`), and the now-confirmed-invalid `D3DRENDERSTATE_TEXTUREMAPBLEND` — a real,
automated proof all these architectural changes are complete throughout, not just claimed. Because
several of DX7's own forbidden symbols legitimately appear in this backend's own explanatory
Doxygen comments, the check's comment-stripping had to be upgraded from DX6's plain `sed 's|//.*||'`
to a real multi-line-aware `perl` block-comment stripper that preserves line numbers.

## 4. Everything else

See `docs/dx6-backend.md` (and, further back, `docs/dx5-backend.md`/`docs/dx3-backend.md`/
`docs/dx2-backend.md`) for the full completeness table — every row applies to `DX7` identically,
since its own code is a port with only the deltas described above.

## See also

- `plan_dx7.md` — this backend's own implementation plan.
- `plan_dx6.md`, `docs/dx6-backend.md` — the backend this one ports.
- `dx7-spike/README.md` — the full `DX7-0` spike record.
- `plan_dxold.md` — the roadmap this backend is row 7 of.
