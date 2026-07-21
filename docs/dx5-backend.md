# DX5 (real DirectDraw v4 + Direct3D v3, FVF `DrawPrimitive`) Backend — Completeness Status

`DX5` is architecturally `DX30` (`docs/dx30-backend.md`) with two upgrades: (1) *every* surface
(not just the top `IDirectDraw` object) moves to v4 — `IDirectDraw4`/`IDirectDrawSurface4`/
`DDSURFACEDESC2`/`DDSCAPS2` — and (2) the 3D layer moves to `IDirect3D3`/`IDirect3DDevice3`/
`IDirect3DViewport3`, submitting `D3DTLVERTEX` via the new `D3DFVF_TLVERTEX` FVF bitmask instead of
the old `D3DVERTEXTYPE` enum (`D3DVT_TLVERTEX`). DX5 (1997) is the first DirectX release where
execute buffers disappear entirely — `IDirect3DDevice3` only ever exposes `DrawPrimitive`/
`DrawIndexedPrimitive`, matching `DX2`/`DX30`'s own already-proven choice of avoiding execute
buffers, except now it's the *only* option the SDK itself offers.

**This document only covers what's specific to `DX5`.** Every capability row, boundary, and test
result in `docs/dx30-backend.md` (and, two generations back, `docs/dx2-backend.md`) applies
identically here (same 2D compositor, same 3D pipeline, same lighting math, same known permanent
limitations) — refer to those for the full completeness table. This document covers: the `DX5-0`
spike, the real code deltas vs. `DX30`, and the DX5-specific CTest/regression results.

---

## 1. Existence-gate spike (`DX5-0`)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX5-0a` | `IDirectDraw4` real 2D surface layer (`CreateSurface`/`Lock`/`Unlock` via `DDSURFACEDESC2`) | Whether v4 is a fully-functional 2D surface layer | ✅ Works |
| `DX5-0b` | `IDirect3D3`/`IDirect3DDevice3::CreateDevice` off a v4 render target | Whether the v4-surface-rooted Direct3D device chain works | ✅ Works |
| `DX5-0c` | `IDirect3DViewport3` + `SetViewport2` (same `D3DVIEWPORT2` struct as `DX2`/`DX30`) | Whether viewport setup is unchanged | ✅ Works |
| `DX5-0d` | `DrawPrimitive` with `D3DFVF_TLVERTEX` instead of `D3DVT_TLVERTEX` | Whether FVF-based submission of the same `D3DTLVERTEX` struct still Gouraud-interpolates | ✅ Works |
| `DX5-0e` | `DrawIndexedPrimitive` + real Z-test, two overlapping quads | Whether depth-test occlusion is real through the new FVF path | ✅ Works — exact `(255,0,0)` readback |
| `DX5-0f` | Real texture sampling from a v4 surface, `IDirect3DTexture2` (unchanged) via a `QueryInterface`'d `IDirect3DDevice2` view of the `device3` object | Whether texture sampling still works when the owning surface is v4 and the device is v3 | ✅ Works |
| `DX5-0g` | `IDirect3DViewport3::Clear2` with explicit color/z/stencil values | Whether `Clear2` is a real, working replacement for `DX2`/`DX30`'s manual Z-buffer `Lock()` workaround | ✅ Works — exact requested clear color/depth read back |

Two real bugs found in the spike *itself*, not in Wine/Direct3D5 (see `dx5-spike/README.md` for
full detail): `Clear2`'s `count=0, rects=nullptr` clears nothing at all (silently) — an explicit
`D3DRECT{0,0,w,h}` with `count=1` is required, same convention the old `Clear()` already used; and
sampling the exact screen-center pixel of a diagonally-split full-viewport quad lands on the
triangle seam and reads a rasterization-edge value, not either triangle's true interior color.

## 2. Real code deltas vs. `DX30`

- **Every surface is v4** (`LPDIRECTDRAWSURFACE4`/`DDSURFACEDESC2`/`DDSCAPS2`), not just the top
  `IDirectDraw4` object.
- **Z-buffer creation**: `DDSURFACEDESC2` dropped the old top-level `dwZBufferBitDepth`/
  `DDSD_ZBUFFERBITDEPTH` — Z-buffer depth is now described via `ddpfPixelFormat.dwFlags =
  DDPF_ZBUFFER` + `ddpfPixelFormat.dwZBufferBitDepth = 16` (spike-confirmed field migration).
- **`IDirectDrawSurface4::Unlock` takes `LPRECT`** (conventionally `nullptr`), not the `LPVOID`
  locked-memory pointer every earlier surface version's `Unlock` takes — a real signature change
  the compiler caught while porting (not a runtime behavior difference, so no separate spike was
  needed for it).
- **`IDirect3D3::CreateDevice`** takes an extra trailing `IUnknown* outer` parameter (always
  `nullptr` here) vs. `IDirect3D2::CreateDevice`.
- **`IDirect3DTexture2::GetHandle`** still only ever accepts an `IDirect3DDevice2*` (never gained
  an `IDirect3DDevice3` overload) — a temporary `IDirect3DDevice2` view is obtained via
  `QueryInterface` on the real `device3` object for exactly this call, then released.
- **`DrawPrimitive`/`DrawIndexedPrimitive`'s vertex-type parameter** is now a `DWORD` FVF bitmask
  (`D3DFVF_TLVERTEX`) instead of the `D3DVERTEXTYPE` enum (`D3DVT_TLVERTEX`) — same `D3DTLVERTEX`
  struct, same CPU transform/clip pipeline, same Phase O9 lighting math.
- **`ClearDepth` uses a real `IDirect3DViewport3::Clear2` call** instead of `DX2`/`DX30`'s manual
  Z-buffer `Lock()`-and-write-raw-values workaround (`FillZBuffer16`, removed as dead code).
  `Clear(r,g,b,a)`'s own color clear is unchanged (still a direct 2D `Lock()`+fill on
  `ActiveSurface()` — a custom-bound `RenderTarget2D` has no Direct3D device/viewport of its own to
  `Clear2` against).

## 3. CTest results

**19/19 `DX5`-labeled CTests pass**, all green on the first run after the port (both real signature
differences above were caught and fixed by the compiler before any test even ran):

`Dx5_ExecuteBufferDiscipline`, `Dx5_Smoke`, `Dx5_TextureRenderTarget`, `Dx5_SpriteBatch`,
`Dx5_Blend`, `Dx5_AddressMode`, `Dx5_SpriteFont`, `Dx5_GraphicsCapability`,
`Dx5_LogicalTransform`, `Dx5_Device3DSmoke`, `Dx5_VertexIndexBuffer`, `Dx5_ColoredPrimitives`,
`Dx5_IndexedPrimitives`, `Dx5_ZTest`, `Dx5_Texture3D`, `Dx5_Clipping`, `Dx5_RemainingDefaults`,
`Dx5_Lighting`, `Dx5_WireframeAniso`.

`SupportsCapability(GraphicsCapability::WireFrame)` reports `true` (inherited from `DX2`/`DX30`'s
Phase O9 finding); `AnisotropicFiltering` stays `false` (same confirmed-absent finding).

Targeted cross-backend regression (mirroring `DX30-83`'s scope decision — a full multi-hour
`CnaTests` regression is out of proportion for this kind of port):
`GraphicsBackendCompileDefinitionsTest.ExactlyOneGraphicsBackendIsSelected`,
`GraphicsDeviceValidationTest.SetRenderTargets_*`, and `GraphicsDeviceCapabilityTest.*` all pass
except the same 3 pre-existing, already-documented ungated-test-class failures `DX2-84` found
(`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects`).

The execute-buffer discipline check (`scripts/check-dx5-execute-buffer-discipline.sh`) is
stricter than `DX30`'s own: it also forbids bare (v1) `IDirectDrawSurface` (every surface this
backend creates is genuinely v4) and the old `D3DVT_*` vertex-type enum values, giving a real,
automated proof both upgrades are complete throughout, not just claimed.

## 4. Everything else

See `docs/dx30-backend.md` (and, two generations back, `docs/dx2-backend.md`) for the full
completeness table — every row applies to `DX5` identically, since its own code is a port with
only the deltas described above.

## See also

- `plan_dx5.md` — this backend's own implementation plan.
- `plan_dx30.md`, `docs/dx30-backend.md` — the backend this one ports.
- `dx5-spike/README.md` — the full `DX5-0` spike record.
- `plan_dxold.md` — the roadmap this backend is row 5 of.
