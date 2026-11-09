# `RasterizerState` support

This page is the contract for the XNA `RasterizerState` object and its renderer consumption.

## Public object contract

The complete XNA surface is present. A default-constructed state has these values:

| Property | Default |
|---|---|
| `CullMode` | `CullCounterClockwiseFace` |
| `DepthBias` | `0.0f` |
| `FillMode` | `Solid` |
| `MultiSampleAntiAlias` | `true` |
| `ScissorTestEnable` | `false` |
| `SlopeScaleDepthBias` | `0.0f` |

The three static presets are `CullClockwise`, `CullCounterClockwise` and `CullNone`. Their `Name`
values are respectively `"RasterizerState.CullClockwise"`,
`"RasterizerState.CullCounterClockwise"` and `"RasterizerState.CullNone"`; an ordinary new state
has an empty name. `GraphicsDevice` starts with the `CullCounterClockwise` preset.

Assigning a state to a device binds and freezes its shared payload, matching measured Microsoft
XNA behavior. Mutating any of the six properties afterward throws through both the caller's
wrapper and the device-owned wrapper. The same shared identity carries the portable
`GraphicsResource` observations (`Name`, `Tag`, disposal/events and parent-device rebinding).
Literal C++ wrapper-address and cross-wrapper event-token identity remain the accepted
`SOFTWARE-198` language-mapping difference recorded in `CHECKLIST.md`.

`GraphicsDevice` applies the state when it is assigned and reapplies it before each draw. The
common renderer seam is deliberately split:

- `ApplyRasterizerState(...)` carries culling, fill, scissor enablement and both depth-bias values;
- `ApplyRasterizerMultiSampleState(...)` carries `MultiSampleAntiAlias` separately.

The second call's default renderer implementation is a no-op; the per-family table below says which
renderers actually consume it.

## Measured Microsoft XNA behavior

### Culling and winding

Measured on Microsoft XNA 4.0 with two opposite-winding triangles (Task 954):

| `CullMode` | Clockwise triangle | Counter-clockwise triangle |
|---|---|---|
| `None` | visible | visible |
| `CullCounterClockwiseFace` (default) | visible | culled |
| `CullClockwiseFace` | culled | visible |

The shared camera and indexed-`BasicEffect` regressions extend this through orthographic and
perspective transforms, positive- and negative-determinant world matrices, bound vertex/index
buffers and `DrawIndexedPrimitives`.

### `MultiSampleAntiAlias`

The retained oracle in `spikes/xna-multisample-antialias-spike/` proves that Microsoft XNA sends
`false` to D3D9 render state 161 (`D3DRS_MULTISAMPLEANTIALIAS`). A control run that never assigns
`false` produces no such write. DXVK reports that state as unhandled, so the probe cannot establish
its pixel effect on native D3D9; it establishes that the property is live and reaches the native
rasterizer.

This property is not the same question as whether a back buffer or render target owns multisample
storage. `GraphicsCapability::MultiSampleAntiAliasing`, presentation sample-count selection and
the MSAA rows in `graphics-renderer-feature-matrix.md` describe target storage. The property selects
independent per-sample rasterization on an already multisampled destination; setting it to `false`
must not silently turn a four-sample target into a one-sample target.

## Renderer matrix

The cells describe implementation routes, not merely API acceptance. “No-op” means the
public value is retained and frozen but has no renderer effect.

| Renderer family | Cull mode | Wireframe | Depth bias | Scissor | `MultiSampleAntiAlias` |
|---|---|---|---|---|---|
| EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`) | native GL culling | conditional native polygon mode; see below | polygon offset, scaled to the active depth format | native | the `OPENGL33` runtime profile toggles `GL_MULTISAMPLE`, also when selected in a multi-renderer build; GLES/WebGL no-op |
| `WEBGPU` | pipeline state | triangle-edge expansion | pipeline state for polygons; dropped for expanded wireframe lines | dynamic per draw | no-op |
| `SDLGPU` | pipeline state | native `SDL_GPU_FILLMODE_LINE` | normalized and baked into the pipeline | dynamic per draw | no-op |
| `SOFTWARE` | CPU rasterizer | CPU edge rasterization | CPU constant/slope offset, including wireframe | CPU clip rectangle | implemented for triangles, lines and wireframe |
| `DIRECTX9` | `D3DRS_CULLMODE` | `D3DRS_FILLMODE` | `D3DRS_DEPTHBIAS` / `D3DRS_SLOPESCALEDEPTHBIAS` | native | written as `D3DRS_MULTISAMPLEANTIALIAS`, as XNA does; pixel effect unmeasured (DXVK ignores the state) |
| `FNA3D` | forwarded in `FNA3D_RasterizerState` | forwarded | forwarded | forwarded | forwarded; the OpenGL driver toggles `GL_MULTISAMPLE` (shared contract passes), D3D11 sets `MultisampleEnable`, the SDL_GPU-based drivers have no switch |
| `HEADLESS` | trace only; no pixels | advertised for validation, no pixels | trace only | trace only | no-op |
| `STUB` | no rasterization | unsupported | no rasterization | no rasterization | no-op |

### Wireframe boundaries

- EasyGL reports `GraphicsCapability::WireFrame` only when the live context exposes desktop
  `glPolygonMode`, `GL_NV_polygon_mode` or `WEBGL_polygon_mode`. That native path preserves
  clipping, culling, depth bias, multisampling and stencil for every triangle route. A narrowly
  bounded stock line-loop fallback remains for simple contexts, but it deliberately does not make
  the capability answer true; general triangle routes fail explicitly when native polygon mode is
  unavailable.
- WebGPU rewrites each queued `TriangleList`/`TriangleStrip` draw to a 32-bit indexed line list.
  Every public 3D route and `SpriteBatch` participates. Line and point primitives are unchanged,
  because a fill mode only selects how a polygon interior is rasterized. WebGPU forbids depth bias
  on line topology, so an expanded wireframe draw drops both bias terms; ordinary polygon draws
  keep them.
- SDLGPU, Software, DirectX9 and FNA3D consume wireframe through their native or CPU rasterizer
  state. HEADLESS makes no pixel claim and STUB reports the capability unsupported.

### Depth bias and scissor

`DepthBias` is a normalized XNA depth offset, while `SlopeScaleDepthBias` multiplies the maximum
polygon depth slope. EasyGL converts the constant term to the active Depth16/Depth24 resolution;
WebGPU and SDLGPU bake the normalized pair into their pipeline descriptors; Software evaluates the
same formula before depth comparison; DirectX9 and FNA3D forward their native float fields.

`ScissorTestEnable` gates `GraphicsDevice.ScissorRectangle`; the rectangle remains stored when the
test is disabled. The effective raster region is the target bounds intersected with the viewport
and, when enabled, the scissor rectangle. Render-target changes reset viewport and scissor to the
new target as described in `rendertarget-support.md`. `Clear` temporarily neutralizes rasterizer
scissor state and covers the requested full target, matching XNA.

### Multisample-rasterization boundary

Software is the complete reference implementation: on a four-sample target, `true` evaluates
coverage and depth at the standard D3D 4x sample positions; `false` evaluates one pixel-center
decision and replicates it to the stored samples. The shared contract covers triangles,
`LineList` and wireframe and verifies `true`/`false`/`true` restoration. Desktop EasyGL expresses
the same state with `GL_MULTISAMPLE`, selected from the device's runtime profile even when the
binary's default is a different renderer. OpenGL ES and WebGL have no equivalent switch and never
receive that desktop-only capability.

FNA3D forwards the property in `FNA3D_RasterizerState` (CNA-FIX-L20): its OpenGL driver toggles
`GL_MULTISAMPLE`, and the shared contract (`Fna3d_MsaaFragmentContract`, registered for the OpenGL
test driver) passes there. D3D11's `MultisampleEnable` changes line rasterization, not triangles,
and FNA3D's SDL_GPU-based drivers record the value without a native switch (they also lack a sample
mask, so the shared contract only partly passes there). DirectX9 writes `D3DRS_MULTISAMPLEANTIALIAS` as XNA does; its
pixel effect is unmeasured because DXVK reports the state unhandled.

WebGPU and SDLGPU inherit the no-op renderer method: neither API has a per-draw multisample
rasterization switch. This is a real fidelity limitation, not proof that the XNA property is
inert. It does not invalidate their separate MSAA-target support: those renderers may still
allocate, rasterize and resolve multisampled targets while ignoring this per-state toggle.

## Evidence

The contract is pinned at several layers:

- `RasterizerStateTest`, `GraphicsDeviceDefaultStateTest` and the assigned/default-state alias
  tests cover defaults, preset names, all setters, device defaults and bound-state freezing;
- the shared cull-mode camera/indexed programs and renderer-specific EasyGL, SDLGPU and DirectX9
  scenes cover the measured winding contract;
- `GraphicsDeviceCapabilityTest` provides the capability-versus-pixels wireframe oracle, while
  `WebGpuWireFrameContract`, `SdlGpu_RenderState`, `Software_Wireframe` and `Fna3d_3D` cover their
  renderer-specific routes;
- `RasterizerDepthBiasContractTest`, `EasyGL_DepthBias`, `SdlGpu_DepthBias` and
  `Software_DepthBias` discriminate constant/slope bias and depth-format scaling;
- EasyGL, WebGPU, SDLGPU, Software, DirectX9 and FNA3D scissor scenes cover state transitions and
  target/viewport intersection;
- `Software_MsaaFragmentContract` and `EasyGL_MsaaFragmentContract` cover the representable
  multisample toggle behavior and the GLES no-equivalent boundary. Linux multi-renderer builds
  additionally register `EasyGL_RuntimeMsaaFragmentContract_*` for compiled nondefault native GL
  profiles and `EasyGL_RuntimeMultiSampleState_*` to inspect the native state and GL errors.

The broader renderer capability overview is `graphics-renderer-feature-matrix.md`;
renderer-specific device/build restrictions are in each renderer page and in `KNOWN_ISSUES.md`.
