# ADR: Keep the Skia renderer 2D-only

Status: Accepted

Date: 2026-08-01

Decision: 2D-only

## Context

SKIA-95 identified 37 stable renderer requirements behind the current EasyGL 3D test surface.
SKIA-96 through SKIA-100 then tested the smallest plausible Skia bridge rather than treating a
coloured triangle as support:

- `SkVertices` cannot retain clip W, perform homogeneous clipping or apply configured culling;
- a bounded CPU RGBA8/depth/stencil raster target can preserve perspective interpolation and the
  depth/stencil state-machine ordering;
- bounded CPU input assembly can decode all current layouts, indices and primitive topologies;
- one unlit/no-fog textured BasicEffect route can reproduce the four EasyGL combined pixels and
  hand the completed image to Skia;
- complete stock effects still require normals, vertex-versus-pixel lighting, fog, cube sampling,
  skinning, PBR and public ownership; custom EasyGL effects require an arbitrary vertex stage;
- MRT, MSAA, anisotropic/LOD sampling and occlusion queries are not supplied by the raster Skia
  canvas.

The successful pieces prove that a new software 3D renderer could eventually be written. They do
not show that Skia provides such a renderer. Completing the bridge would duplicate the existing
Software/EasyGL renderer responsibilities inside the Skia renderer, while adding a second set of
coverage, sampler, shader, state, resource and effect rules to maintain.

## Decision

The Skia renderer remains a deterministic 2D raster renderer. It will not expose a CPU-emulated 3D
pipeline and will not advertise `GraphicsCapability::ThreeD`, real depth/stencil, wireframe,
arbitrary stock 3D effects or custom 3D effects.

The SKIA-96--100 prototypes remain tests and design evidence. They are not moved into product code.
Proven 2D behavior, fragment-only tagged SkSL, TextureCube/Texture3D transfer storage, and
RenderTargetCube face rendering remain available within their documented non-3D contracts.

SKIA-102 will make public 3D refusal uniform and exhaustive without breaking those retained
contracts. SKIA-103 is obsolete under this decision. A future reversal requires a new ADR and a
successor plan covering each live requirement independently before any implementation or
capability change.

## Alternatives considered

### Finish a CPU renderer inside the Skia renderer

Rejected. The remaining work is the renderer itself: homogeneous clipping, production triangle/
line/point coverage, cube/volume/mip sampling, MSAA, all state interactions, seven stock-effect
families, arbitrary vertex programs, model/skinning integration, resource lifetime and queries.
Skia would only receive the finished pixels and would not reduce that complexity.

### Delegate 3D commands to the Software renderer and composite into Skia

Rejected. CNA constructs one graphics renderer and its resource renderers have concrete ownership.
A hidden second renderer would require cross-renderer texture/buffer/effect mirroring, synchronization
for every upload and target transition, two independent state machines, mixed 2D/3D ordering, and
new lifetime/error rules. This is broader and less transparent than selecting the Software renderer
when CPU 3D is desired.

### Create an EasyGL context for 3D while retaining Skia for 2D

Rejected. That creates a new hybrid accelerated renderer with platform context sharing, target
interop and synchronization requirements. Applications that need EasyGL's 3D contract can select
EasyGL directly; silently embedding it would make the Skia raster renderer neither deterministic
nor dependency-isolated.

### Keep isolated prototypes but reject public 3D

Accepted. It preserves the verified 2D value of the renderer, keeps unsupported behavior explicit,
and avoids advertising a partial 3D surface whose failures depend on layout, effect or state.

## Requirement decisions

Dispositions have exact meanings:

- `prototype-only`: useful isolated test evidence remains, but the corresponding public 3D route
  must reject;
- `2d-only`: an existing 2D analogue remains supported, but it must not be accepted as 3D evidence;
- `transfer-only`: CPU cube/volume transfer storage remains, while shader sampling/3D use rejects;
- `bounded-2d-sampling`: added by SKIA-144–151 (`docs/skia-cube-volume-sampling-contract.md`) after
  this ADR was accepted -- CPU transfer storage remains as `transfer-only` describes, and a
  separate, narrow, fragment-only 2D SkSL extension (`cnaSampleCubeEXT`/`cnaSampleVolumeEXT`) now
  also samples that storage, but general/stock 3D shader sampling still rejects exactly as before;
- `reject`: no positive Skia contract exists for the requirement.

| Feature ID | Disposition | Evidence behind the decision | SKIA-102 consequence |
|---|---|---|---|
| `3D-VERTEX-LAYOUT` | `prototype-only` | SKIA-99 decodes seven built-ins, 12 formats and 13 usages only inside a test. | Keep public vertex-buffer creation/draw routes unsupported. |
| `3D-VERTEX-BUFFER` | `prototype-only` | Bounded replacement uploads are feasible, but no public ownership/readback/lifetime path exists. | Use one deterministic creation failure for static and dynamic buffers. |
| `3D-INDEX-16` | `prototype-only` | SKIA-99 preserves 16-bit offsets and fetch in isolation. | Reject public index-buffer creation before mutation. |
| `3D-INDEX-32` | `prototype-only` | SKIA-99 fetches vertex 70000 without truncation in isolation. | Reject the same way as 16-bit indices. |
| `3D-DRAW-USER` | `prototype-only` | Raw/four typed/indexed input assembly passes only below public Draw calls. | All DrawUser overloads must fail uniformly and leave targets unchanged. |
| `3D-PRIMITIVE-TRIANGLE` | `prototype-only` | Assembly and scalar coverage spikes are not production top-left/clipping behavior. | Reject triangle draws rather than expose approximate coverage. |
| `3D-PRIMITIVE-STRIP` | `prototype-only` | Alternating winding expansion passes; coverage remains the triangle gap. | Reject strip draws through the common unsupported path. |
| `3D-PRIMITIVE-LINE` | `prototype-only` | List/strip assembly passes, but exact endpoint/diamond-exit and point rules do not exist. | Reject line and point draws instead of rasterizing approximate pixels. |
| `3D-INSTANCING` | `reject` | No multiple-stream/instance-frequency CPU input or public resource route was prototyped. | Reject instanced draws deterministically. |
| `3D-TRANSFORM` | `prototype-only` | CPU WVP/viewport math passes for bounded fixtures. | Do not expose transforms without the complete clip/raster/effect route. |
| `3D-CLIP-INTERPOLATE` | `prototype-only` | SKIA-96 proves direct SkVertices affine and near-plane mismatches; CPU clipping is incomplete. | Reject input requiring the absent public pipeline. |
| `3D-TEXTURE-2D` | `2d-only` | Texture2D and SpriteBatch sampling are proven; geometry derivatives/LOD are not. | Retain 2D texture APIs, reject their use by public 3D draws. |
| `3D-TEXTURE-CUBE` | `bounded-2d-sampling` | Six-face/mip CPU transfer storage and face rendering exist; SKIA-144–151 additionally proved bounded direction-to-face sampling through the explicit fragment-only `cnaSampleCubeEXT` extension -- not a general 3D sampler. | Retain transfers/face targets; `BindTextureCube` now binds for the bounded SkSL extension only (unit/undeclared-children/null/expired checks), still rejecting for any general/stock 3D shader path. |
| `3D-TEXTURE-VOLUME` | `bounded-2d-sampling` | Mip/slice/box CPU transfer storage exists; SKIA-144–151 additionally proved bounded 3D-coordinate trilinear sampling through the explicit fragment-only `cnaSampleVolumeEXT` extension -- not a general 3D sampler. | Retain transfers; `BindTexture3D` now binds for the bounded SkSL extension only (unit/undeclared-children/null/expired/atlas-budget checks), still rejecting for any general/stock 3D shader path. |
| `3D-SAMPLER-MIP` | `reject` | Raster 2D SpriteBatch mipmaps and affine LOD are supported, but geometry derivatives and the public 3D sampler/effect route remain absent. | Retain the verified 2D mip route; reject 3D sampling and never substitute level zero silently. |
| `3D-SAMPLER-ANISOTROPY` | `reject` | The capability is false and SpriteBatch has an exact complete-Linear fallback including mips; the stock-3D route remains absent. | Keep 3D sampling rejected and do not reinterpret the 2D fallback as hardware anisotropy. |
| `3D-STATE-DEPTH` | `prototype-only` | SKIA-97 proves one float LessEqual/write buffer, not public depth formats/bias. | Reject depth attachments/3D clears through a consistent boundary. |
| `3D-STATE-STENCIL` | `prototype-only` | SKIA-98 proves the 8-bit state machine but no public attachment. | Reject public stencil attachment/use while retaining false capability. |
| `3D-STATE-CULL-FILL` | `prototype-only` | SKIA-99 proves winding/cull/wire assembly, not pixel rules or bias. | Reject public 3D draw; do not advertise wireframe. |
| `3D-STATE-BLEND-COLOR` | `2d-only` | 2D blend/color-write paths are pixel-proven; only opaque effect geometry was prototyped. | Retain 2D state behavior, reject its interpretation as a 3D draw path. |
| `3D-STATE-MRT` | `reject` | SKIA-87 proves one canvas cannot recover multiple independent fragment outputs. | Continue atomic refusal of multiple active attachments. |
| `3D-STATE-MSAA` | `reject` | Raster sample requests above one reject and no depth-coupled resolve exists. | Keep MSAA capability false and reject unsupported requests. |
| `3D-STATE-ORDER` | `2d-only` | SpriteBatch/target ordering is proven; mixed 2D/3D command ordering is absent. | Retain 2D ordering and ensure failed 3D calls cannot alter it. |
| `3D-VIEWPORT-SCISSOR` | `2d-only` | Canvas viewport/scissor behavior is proven only for raster draws. | Retain 2D behavior; failed 3D calls leave both states unchanged. |
| `3D-FX-BASIC` | `prototype-only` | SKIA-100 matches one unlit/no-fog textured PCT route; lighting and variants are absent. | Effect properties may exist, but every public geometry draw must reject. |
| `3D-FX-ALPHATEST` | `prototype-only` | Compare/discard pieces pass in isolation without depth/stencil/fog integration. | Do not expose the stock effect as a working 3D draw route. |
| `3D-FX-DUAL` | `prototype-only` | Two-child formula/addressing passes without full sampler/fog/coverage integration. | Do not expose the stock effect as a working 3D draw route. |
| `3D-FX-ENVMAP` | `reject` | The bounded `cnaSampleCubeEXT` direction lookup itself now exists (`3D-TEXTURE-CUBE` above) but is not wired to `EnvironmentMapEffect`; normal/eye transforms, Fresnel and lighting remain absent regardless. | Reject environment-map geometry uniformly; the bounded cube-sampling primitive existing does not promote this stock effect. |
| `3D-FX-SKINNED` | `reject` | Layout/palette data exist, but weighted position/normal/fog/lighting do not. | Reject skinned geometry without partial deformation. |
| `3D-FX-PBR` | `reject` | TBN, five samplers, BRDF and skinned variant are absent. | Reject PBR and SkinnedPBR geometry uniformly. |
| `3D-FX-CUSTOM` | `2d-only` | Tagged SkSL is fragment-only; arbitrary EasyGL GLSL vertex/fragment programs are incompatible. | Retain explicit 2D SkSL, reject custom 3D effect draws/source assumptions. |
| `3D-SHADE-LIGHT` | `reject` | Normal transforms and vertex/pixel lighting variants have no CPU pipeline. | Never ignore LightingEnabled, normals, lights or specular settings. |
| `3D-SHADE-FOG` | `reject` | Fog vectors exist, but pre/post-skin evaluation and interpolation are absent. | Never ignore enabled fog on an attempted 3D draw. |
| `3D-MODEL-SKIN` | `reject` | Content/property data can load, but model mesh and skeleton rendering is absent. | Model draws fail at the same renderer 3D boundary as direct draws. |
| `3D-QUERY-OCCLUSION` | `reject` | Raster Skia has no asynchronous visibility/query lifecycle. | Keep query capability false and Begin/End deterministically unsupported. |
| `3D-RESOURCE-CONTRACT` | `2d-only` | Proven 2D/transfer resources retain bounded lifetimes; no 3D resource graph exists. | Fail 3D creation/binding atomically without breaking retained resources. |
| `3D-TARGET-PASS` | `2d-only` | Colour-only 2D/cube-face passes work; depth/MRT/3D producer-consumer passes do not. | Retain those passes and guarantee failed 3D work cannot clear or dirty them. |

## Consequences

- SKIA-102 is now the active implementation task. It must audit every public 3D entry point and
  prove deterministic, atomic refusal while retaining documented 2D/transfer behavior.
- SKIA-103 is marked obsolete because no fully funded 3D implementation phase is accepted.
- SKIA-104/105 may still examine occlusion-query refusal quality, but cannot use this ADR as
  permission to build a partial 3D pipeline.
- Any future proposal to support Skia 3D must replace this ADR, cover all then-live requirement
  IDs, define a funded acceptance suite, and explain why selecting EasyGL/Software is insufficient.
