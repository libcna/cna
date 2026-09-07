# Software <-> EasyGL XNA/Core parity ledger

This ledger is the durable evidence map for the parity campaign in
`plans/plan_software.md`. It was started on 2026-09-07 at CNA commit
`cde325ecd4d771d50b7d8ec3ef99f1657ad217ac` on branch `software`.

The behavioral authority order is measured Microsoft XNA 4.0/IL, FNA, EasyGL, current Software,
then prose. EasyGL is the CNA coverage reference, not permission to reproduce a demonstrated
EasyGL bug. A similarly named implementation method is not evidence. A test that succeeds by
recording an unsupported boundary is gap evidence, not parity evidence.

## Classification vocabulary

- `IN-SCOPE-XNA`: public XNA 4.0 behavior.
- `IN-SCOPE-CORE`: renderer-agnostic CNA plumbing required to make the XNA surface real.
- `DEFERRED-CNAEXT`: modern/CNAEXT behavior excluded from this campaign.
- `NOT-APPLICABLE-SOFTWARE`: physical GPU/window behavior with no meaningful CPU-renderer analog.
- `ALREADY-PARITY`: implementation and behavioral evidence already support the parity claim.
- `REAL-GAP`: in-scope behavior is absent, incomplete, silently degraded, or only boundary-tested.

The scope and verdict columns are intentionally separate: for example, a feature can be
`IN-SCOPE-XNA` and `REAL-GAP`, or `IN-SCOPE-XNA` and `ALREADY-PARITY`.

## Repository evidence census

- EasyGL owns 246 renderer example/test translation units. These include classic XNA behavior,
  renderer-native GL details, and modern CNAEXT shader/PBR examples; filenames are therefore an
  audit corpus, not an automatic scope list.
- Software owns 16 local example/test translation units and registers 60 `Software` CTests at the
  starting commit. Many of those 60 compile shared graphics fixtures, which is stronger evidence
  than the local-file count suggests.
- Later remediation tests already cover depth state, viewport/scissor, culling, wireframe, depth
  bias, exact indexed addressing, render-target readback/ordering/lifetime, blend equations/write
  masks, sampler component isolation, mip filters, texture/cube transfers and descriptor capacity.
- The registered shared TriangleStrip and RenderTargetCube fixtures deliberately accept and report
  Software's old unsupported boundary. They do not establish parity.

## Renderer contract and public XNA/Core ledger

| Feature family | EasyGL evidence | Software evidence | Scope | Verdict | Task | Verification / next proof |
|---|---|---|---|---|---|---|
| Renderer creation, no-display CPU operation, clear/readback | `easygl_goldenimage_smoke_test.cpp`, pixel fixtures | `software_smoke_test.cpp`, backbuffer readback/first-read/reject shared tests | IN-SCOPE-CORE | ALREADY-PARITY for meaningful CPU behavior | SOFTWARE-100 | Re-run clean baseline; physical GL context is not required of Software. |
| Physical window, swap interval, present interval, context lease/loss | EasyGL real-window/vsync/context tests | Software intentionally owns no native window/context and `Present()` is immediate CPU completion | NOT-APPLICABLE-SOFTWARE | Classified intentional difference | SOFTWARE-124/127 | Test public state propagation only where meaningful; never fabricate a GPU context. |
| Capability/factory consistency | EasyGL explicitly probes device-dependent MRT/query/Texture3D/instancing/aniso | Software returns true by default for OcclusionQuery and AnisotropicFiltering while query creation is null and anisotropic sampling degrades to linear | IN-SCOPE-CORE | REAL-GAP | SOFTWARE-104 | Shared capability test must pair each true value with successful behavior. |
| TriangleList indexed/non-indexed | broad EasyGL draw/user/buffer corpus | rasterizer/effects/indexed-addressing/front-face tests | IN-SCOPE-XNA | ALREADY-PARITY for canonical declarations; declaration limitation tracked separately | SOFTWARE-108/109 | Retain exact start/count/base/16/32-bit coverage. |
| TriangleStrip indexed/non-indexed | shared `triangle_strip_winding_test.cpp` executes EasyGL paths | same fixture records Software's explicit unsupported boundary | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-105 | Execute full six-path, odd/even, three-cull matrix with zero Software boundaries. |
| LineList/LineStrip | EasyGL primitive tests | Software effect-aware routes implement both; limited direct proof in later shared suites | IN-SCOPE-XNA | Audit evidence incomplete | SOFTWARE-103/109 | Add shared pixels plus offsets/index widths/range validation. |
| PointListEXT | renderer tests | Software effect-aware route exists | DEFERRED-CNAEXT | Existing support retained; no expansion required | -- | Regression only when shared raster changes touch it. |
| Homogeneous frustum clipping | GL hardware clipping across normal test corpus | only `w > epsilon` Sutherland-Hodgman triangle/line clipping in `SoftwareRenderer.cpp`; local clipping test covers this single plane | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-106 | Six planes, multi-plane and finite-output pixel probes. |
| Fill convention/shared edges | GL/D3D-compatible raster rules; SpriteBatch and blend golden tests | inclusive edge tests plus explicit diagonal masks, not a general top-left rule | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-107 | Adjacent triangles with alpha/additive blend, order reversal and MSAA. |
| Perspective interpolation, viewport/depth range, scissor | EasyGL viewport/scissor/texture tests | perspective correction in raster core; Software custom viewport and scissor pixel tests | IN-SCOPE-XNA | ALREADY-PARITY for covered layouts | SOFTWARE-103/106 | Cross-renderer probes after full clipping. |
| CullMode, wireframe, depth bias | EasyGL cull/depth-bias suite and shared winding fixture | Software culling/wireframe/depth-bias/front-face tests | IN-SCOPE-XNA | ALREADY-PARITY at current evidence level | -- | Retain across strip/top-left/clipping changes. |
| MSAA color coverage/resolve | EasyGL MSAA tests | Software 4-sample color plane and shared RT MSAA/readback tests | IN-SCOPE-XNA | Partial; depth/stencil remain per-pixel | SOFTWARE-110 | Per-sample intersection/mask/depth/stencil proof. |
| VertexDeclaration-driven input | EasyGL vertex formats/custom user layouts and declaration-derived GL attributes | Software stores declarations but dispatches a hard-coded combined-stride table; fidelity guard rejects rather than misinterprets unknown layouts | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-108 | Arbitrary valid reordered/padded/format layouts, including equal-stride ambiguity. |
| Multiple vertex streams/offsets | EasyGL REMED-GFX-201 implementation | Software `CombinedVertexReader`, stream-local strides/offsets and capability opt-in | IN-SCOPE-XNA | ALREADY-PARITY for ordinary per-vertex streams | SOFTWARE-108/109 | Preserve in declaration translator; add focused public differential pixels. |
| Instancing | EasyGL instancing path | Software reports false and inherits explicit throwing default | IN-SCOPE-XNA | REAL-GAP, lower priority than ordinary vertex correctness | Future split after SOFTWARE-108 | Create actionable implementation row after declaration translator establishes reusable streams; do not relabel as CNAEXT. |
| Buffer `SetDataOptions`, dynamic updates, readback, disposal and validation | EasyGL buffer usage/stress/getdata/range/disposed tests | Software has CPU storage and REMED-GFX indexed validation, but EasyGL corpus is not fully mirrored | IN-SCOPE-XNA | Audit incomplete | SOFTWARE-109/124 | Convert behavior fixtures to shared tests and repair observed mismatches. |
| BlendState factors/functions/BlendFactor | EasyGL opaque/alpha/additive/nonpremultiplied/separate-factor/function tests | Software `SoftwareBlendState`, shared additive contract; implementation handles all factors/functions | IN-SCOPE-XNA | ALREADY-PARITY at equation level; stale docs disagree | SOFTWARE-128 | Add/port differential matrix before final campaign closure. |
| ColorWriteChannels/MultiSampleMask | EasyGL color-write tests | Software color-write test and state implementation | IN-SCOPE-XNA | ALREADY-PARITY for one target; MRT slots pending | SOFTWARE-110/120 | Preserve single target; test all four masks once MRT lands. |
| DepthStencilState depth compare/write | EasyGL compare/write golden tests | Software depth-contract/depthstate suite covers all compare functions | IN-SCOPE-XNA | ALREADY-PARITY | -- | Regression under alpha, MSAA and clipping tasks. |
| Stencil masks/ops/reference/two-sided | EasyGL stencil enable/mask/ops/twosided/reference tests | SpriteBatch supplies one-sided Software stencil state, but all 3D calls pass `RasterStencilState{}` and `ApplyDepthStencilState` discards CCW fields | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-121 | Shared full matrix across every 3D topology/path. |
| Sampler point/linear, min/mag/mip, U/V wrap/clamp/mirror, slot independence | EasyGL filter/address/mip/sampler effect tests | REMED-GFX-150/175/182 Software sampler contract, ordinal, component, mip and cube/dual-slot tests | IN-SCOPE-XNA | ALREADY-PARITY for implemented 2D/cube paths | -- | Preserve during resource/effect work; add differential images. |
| Anisotropic sampling | EasyGL GL-state/effect/single-level tests | Software stores no anisotropy and treats the filter as linear while capability defaults true | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-104/117 | Oblique minification plus `MaxAnisotropy`/slot/mip/address tests. |
| Texture2D mips/partial transfer/NPOT | EasyGL mip/partial/NPOT/readback tests | Software texture storage plus shared GetData/SetData/mip/filter tests | IN-SCOPE-XNA | Substantial parity; NPOT/format matrix still needs direct reconciliation | SOFTWARE-126 | Run shared transfer cases and add NPOT sampling differential. |
| TextureCube faces/mips/partial transfer/sampling | EasyGL cube face/mip/content tests | Software cube mip/face storage and cube sampler contracts | IN-SCOPE-XNA | Substantial parity for RGBA8 | SOFTWARE-126 | Add partial rectangle and per-face address/filter differential proof. |
| Texture3D storage/transfer/sampling | EasyGL mip/partial-box/slices tests | Software reports false and leaves factory null | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-118 | CPU mip volume plus W sampler semantics; CNAEXT ShaderEffect-only sampling excluded. |
| Texture/RT packed and compressed formats | EasyGL DXT/packed16/surface-format tests | Software fundamentally stores RGBA8 and relies on shared conversion in several paths | IN-SCOPE-XNA | Audit incomplete, likely gaps | SOFTWARE-126 | Enumerate XNA formats and test accepted bytes/sampling or deterministic rejection. |
| RenderTarget2D sampling/readback/mips/usage/lifecycle | EasyGL RT/readback/mip/usage suite and shared contracts | Software RT surface, mip generation, target sampling, ordered-pass/readback/lifetime tests | IN-SCOPE-XNA | ALREADY-PARITY for RGBA8 single target at current evidence level | SOFTWARE-110/126 | Preserve while strengthening MSAA and formats. |
| RenderTargetCube | EasyGL properties/depth/sample plus shared cube contracts | Software factory is null; registered shared tests accept the boundary | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-119 | All six faces, mip/preserve/depth/stencil/MSAA/readback shared tests. |
| Multiple render targets | `easygl_mrt_test.cpp` | Software explicitly throws for count > 1 and reports false | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-120 | Public validation, output semantics and independent attachments. |
| BasicEffect unlit combinations | EasyGL combinations/golden/texture/vertex-color tests | Software texture/vertex color/diffuse/alpha fixed path and basic local effects test | IN-SCOPE-XNA | Partial | SOFTWARE-103/113 | Differential matrix, including clamping and null texture behavior. |
| BasicEffect lighting | EasyGL default/one/multilight/ambient/emissive/specular/per-pixel/world-scale tests | parameters are carried in `GpuDrawParams` but unused by Software shading | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-113 | XNA/FNA lighting equation and inverse-transpose normals. |
| AlphaTestEffect | EasyGL modes/compare sweep/fog/vertex-color/diffuse/golden tests; FNA `AlphaTestEffect.fx` | Software receives `alphaTest` parameters but never rejects a fragment | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-111/112 | All compares and discard ordering, then fog. |
| DualTextureEffect | EasyGL doubling/independent-UV/null/alpha/fog/golden tests | Software samples two slots but reuses UV0; slot sampler independence is tested | IN-SCOPE-XNA | Partial; independent coordinates and fog absent | SOFTWARE-112/116 | Declaration-driven UV0/UV1, formula, alpha/fog differential. |
| EnvironmentMapEffect | EasyGL amount/fresnel/eye/world/multilight/specular/fog tests | Software cube reflection exists, but lighting/fog absent and World normal transform is not inverse-transpose | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-112/114 | Focused lighting/reflection/normal-scale probes. |
| SkinnedEffect | EasyGL bones/weights/lighting/specular/fog/world-normal tests | Software bone positions and basic normal transform exist; lighting/fog/per-pixel and full normal semantics absent | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-112/115 | 1/2/4-weight and lit/fog/model probes. |
| SpriteEffect | EasyGL/SpriteBatch shader path and SpriteEffect public implementation | Software SpriteBatch uses fixed shaded triangles | IN-SCOPE-XNA | Audit incomplete | SOFTWARE-123 | Shared SpriteBatch matrix indirectly proves SpriteEffect contract. |
| Arbitrary ShaderEffect and custom shader examples | numerous EasyGL shader/bloom/distort/shadow examples | Software intentionally accepts source without executing it and truthfully reports `ExecutesShaderEffectSourceEXT=false` | DEFERRED-CNAEXT | Excluded; existing fallback must not regress | -- | No parity implementation in this campaign. |
| PbrEffect/SkinnedPbrEffect/glTF modern shading | EasyGL PBR/glTF examples | Software retains limited existing fallback fields | DEFERRED-CNAEXT | Excluded | -- | Regression only; no new modern renderer work. |
| SpriteBatch geometry, sorting, transform and state | EasyGL rotation/scale/source/layer/transform/state-leak/RT-size tests | Software viewport/rasterizer tests plus basic draw; broad EasyGL corpus not mirrored | IN-SCOPE-XNA | Audit incomplete, likely gaps | SOFTWARE-123 | Convert reusable EasyGL public fixtures to shared differential tests. |
| SpriteFont layout | EasyGL glyph/spacing/newline/default-char/flip/rotation/scale tests | no equivalent Software-specific proof found in initial inventory | IN-SCOPE-XNA | REAL-GAP in evidence; implementation result unknown | SOFTWARE-123 | Shared DrawString pixel/layout fixtures, then repair mismatches. |
| OcclusionQuery | EasyGL visible/occluded quad tests and real query object | Software inherits null factory while capability defaults true | IN-SCOPE-XNA | REAL-GAP | SOFTWARE-104/122 | Deterministic surviving-sample count and lifecycle. |
| GraphicsDevice/resource lifecycle, reset/events/validation | EasyGL dispose/order/events/range/presentation corpus | some shared lifetime/present tests run on Software; full corpus not reconciled | IN-SCOPE-XNA / IN-SCOPE-CORE | Audit incomplete | SOFTWARE-124/127 | Share public fixtures; classify physical-device-only observations separately. |
| `Model.Draw()` rigid/skinned orchestration | EasyGL model draw/hierarchy/two-mesh/skinned playback tests | only lower-level Software SkinnedEffect raster proof | IN-SCOPE-XNA | REAL-GAP in end-to-end evidence | SOFTWARE-125 | Shared deterministic model construction and readback. |

## Deferred EasyGL example families

The following EasyGL families were explicitly classified rather than silently treated as parity
requirements: bloom, blur, distortion, cartoon, clouds, particle, normal mapping, shadow mapping,
post-processing, custom shader-effect uniforms/layouts/cube/volume sampling, PBR/SkinnedPBR,
modern glTF material/transmission/tangent extensions, storage/compute/indirect draw and HDR engine
facilities are `DEFERRED-CNAEXT`. Existing Software behavior on shared paths must remain stable, but
extending those facilities is not a campaign completion requirement.

EasyGL's real-window resize, actual swap/present interval, native GL context lease, GL handle/state,
driver extension and context-loss tests are `NOT-APPLICABLE-SOFTWARE` except where they reveal a
renderer-independent public GraphicsDevice/resource contract. Those public portions are tracked in
`SOFTWARE-124/127`; Software will not invent fake window, GPU, or driver behavior.
