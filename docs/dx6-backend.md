# DX6 (real DirectDraw v4 + Direct3D v3, real stencil buffer) Backend — Completeness Status

`DX6` is architecturally `DX5` (`docs/dx5-backend.md`) with **no new COM interface at all** —
confirmed by inspecting the real MinGW headers: no `IDirect3D4`/`IDirect3DDevice4` exists. DX6
(1998) reuses `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`/`IDirectDraw4` verbatim; its
entire delta vs. `DX5` is new render states/capabilities on the *same* interface: real stencil
buffer operations (this backend's primary deliverable), multitexturing, and DXTn compression.

**This document only covers what's specific to `DX6`.** Every capability row, boundary, and test
result in `docs/dx5-backend.md` (and, further back, `docs/dx30-backend.md`/`docs/dx2-backend.md`)
applies identically here (same 2D compositor, same 3D pipeline, same lighting math, same known
permanent limitations) — refer to those for the full completeness table. This document covers: the
`DX6-0` spike, the real code deltas vs. `DX5`, and the DX6-specific CTest/regression results.

---

## 1. Existence-gate spike (`DX6-0`)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX6-0a` | `CreateSurface` with `DDPF_ZBUFFER\|DDPF_STENCILBUFFER`, `dwZBufferBitDepth=32`, `dwStencilBitDepth=8`, then `AddAttachedSurface`+`CreateDevice` off it | Whether a stencil-capable Z-buffer surface and device are real, not stubs | ✅ Works |
| `DX6-0b` | `Clear2` with an explicit stencil value (0), then a left-half-only quad with `STENCILFUNC=ALWAYS`, `STENCILPASS=REPLACE`, `STENCILREF=1` | Whether a real stencil WRITE happens | ✅ Works — left half shows the drawn color, right half stays the `Clear2` background |
| `DX6-0c` | A full-screen quad with `STENCILFUNC=EQUAL`, `STENCILREF=1` drawn on top of `DX6-0b`'s result | Whether a real stencil TEST correctly gates the draw per-pixel | ✅ Works — left half (stencil==1) shows the new color, right half (stencil==0) correctly rejects the draw |

Every readback matched its predicted value exactly on the first run — no spike-authoring bugs this
time (unlike `dx5-spike`'s own `Clear2`-rect and diagonal-seam gotchas, both correctly avoided here
having already been learned). See `dx6-spike/README.md` for the full record.

## 2. Real code deltas vs. `DX5`

- **Z-buffer surface is now combined depth+stencil**: `ddpfPixelFormat.dwFlags = DDPF_ZBUFFER |
  DDPF_STENCILBUFFER`, `dwZBufferBitDepth = 32` (total bits), `dwStencilBitDepth = 8` — a
  D24S8-equivalent shape, replacing `DX5`'s depth-only 16-bit Z-buffer. This is the one real
  surface-format change; everything else about surface/device creation is unchanged from `DX5`.
- **`ApplyDepthStencilState` now wires real stencil render states** instead of accepting and
  ignoring them: `D3DRENDERSTATE_STENCILENABLE`, `STENCILFUNC`, `STENCILFAIL`, `STENCILZFAIL`,
  `STENCILPASS`, `STENCILREF`, `STENCILMASK`, `STENCILWRITEMASK` are all set on `IDirect3DDevice3`
  from the corresponding `DepthStencilState` fields. `twoSidedStencilMode`/`ccwStencil*` remain
  accepted-and-ignored — two-sided stencil doesn't exist at this DirectX era at all (a D3D9-era
  addition, confirmed by header inspection).
- **New `Dx6StencilOperationToD3D` helper** maps `Microsoft::Xna::Framework::Graphics::
  StencilOperation` to `D3DSTENCILOP_*` (`Keep`/`Zero`/`Replace`/`Increment→INCR`/
  `Decrement→DECR`/`IncrementSaturation→INCRSAT`/`DecrementSaturation→DECRSAT`/`Invert`).
- **`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`** are
  now real (previously no-ops/partial on `DX5`, which had no stencil aspect to clear at all): all
  route through `IDirect3DViewport3::Clear2` with the `D3DCLEAR_STENCIL` flag, same `D3DRECT`
  full-target-rect convention `DX5`'s own `ClearDepth` already established.
- **Multitexture deliberately deferred, not implemented.** `SetTextureStageState`/`D3DTSS_*` are
  real per the headers, but `D3DTLVERTEX`/`D3DFVF_TLVERTEX` (the vertex format this whole backend
  family's CPU transform/clip pipeline is built around) carries only one texture-coordinate pair
  (`D3DFVF_TEX1`). Genuine two-independent-UV multitexture (what `DualTextureEffect` needs) would
  require a second vertex layout (`D3DFVF_TEX2`) and extending the whole CPU pipeline — out of
  proportion for this backend, documented rather than silently dropped.
- **DXTn compression out of scope**: no real caller-facing gap, since CNA's content pipeline never
  feeds compressed texture data into any legacy DirectX backend's texture-upload path (same
  no-consumer reasoning already used for `DX30`'s `GetAvailableVidMem` and `DX5`'s real vertex
  buffers).
- Everything else — 2D layer, 3D device bring-up, CPU transform/clip pipeline, `D3DFVF_TLVERTEX`
  submission, Phase O9 lighting, `VertexBuffer`/`IndexBuffer` backends, remaining
  `IGraphicsBackend` defaults — is an unmodified port of `DX5`'s own code.

## 3. CTest results

**20/20 `DX6`-labeled CTests pass**, all green on the first run after fixing the CMake registry
wiring (no backend-code compile errors at all this time — both real signature differences DX5 had
already caught were, correctly, non-issues here since no interface changed):

`Dx6_ExecuteBufferDiscipline`, `Dx6_Smoke`, `Dx6_TextureRenderTarget`, `Dx6_SpriteBatch`,
`Dx6_Blend`, `Dx6_AddressMode`, `Dx6_SpriteFont`, `Dx6_GraphicsCapability`,
`Dx6_LogicalTransform`, `Dx6_Device3DSmoke`, `Dx6_VertexIndexBuffer`, `Dx6_ColoredPrimitives`,
`Dx6_IndexedPrimitives`, `Dx6_ZTest`, `Dx6_Texture3D`, `Dx6_Clipping`, `Dx6_RemainingDefaults`,
`Dx6_Lighting`, `Dx6_WireframeAniso`, and the new **`Dx6_Stencil`** (real stencil write-then-test
through the full XNA public API — `GraphicsDevice.DepthStencilState`, not just the raw spike; 4/4
checks pass: stamp-left/stamp-right/test-left-passes/test-right-rejected).

`SupportsCapability(GraphicsCapability::WireFrame)` reports `true` (inherited from `DX5`'s own
Phase O9 finding); `AnisotropicFiltering` stays `false` (same confirmed-absent finding).
`SupportsCapability(GraphicsCapability::DepthStencilBuffer)` reports `true`, now backed by a real
stencil aspect rather than depth-only.

Targeted cross-backend regression (mirroring `DX5-84`'s own scope decision):
`GraphicsBackendCompileDefinitionsTest.ExactlyOneGraphicsBackendIsSelected`,
`GraphicsDeviceValidationTest.SetRenderTargets_*`, and `GraphicsDeviceCapabilityTest.*` all pass
except the same 3 pre-existing, already-documented ungated-test-class failures `DX2-84` found
(`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects`) — zero new
regressions.

The execute-buffer discipline check (`scripts/check-dx6-execute-buffer-discipline.sh`) is adapted
verbatim from `DX5`'s own: identical forbidden-symbol set (execute buffers, the un-versioned
`IDirect3D`/`IDirect3DDevice`, `IDirectDraw[237]`, bare `IDirectDrawSurface`, the old `D3DVT_*`
vertex-type enum), since `DX6` introduces no new interface to add rules for.

## 4. Everything else

See `docs/dx5-backend.md` (and, further back, `docs/dx30-backend.md`/`docs/dx2-backend.md`) for
the full completeness table — every row applies to `DX6` identically, since its own code is a port
with only the stencil-related deltas described above.

## See also

- `plan_dx6.md` — this backend's own implementation plan.
- `plan_dx5.md`, `docs/dx5-backend.md` — the backend this one ports.
- `dx6-spike/README.md` — the full `DX6-0` spike record.
- `plan_dxold.md` — the roadmap this backend is row 6 of.
