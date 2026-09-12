# RLGL Renderer Plan

**Status:** Active — architecture audit complete; implementation baseline not started  
**Target identity:** `RLGL`  
**Primary parity reference:** EasyGL  
**Initial profile:** OpenGL 3.3 core  
**Upstream baseline:** raylib 6.0, commit `dbc56a87da87d973a9c5baa4e7438a9d20121d28`, standalone `src/rlgl.h`

## Status legend

- ✅ **Completed** — implementation or evidence exists and the recorded validation passed.
- 🟨 **In progress** — work exists, but the task's validation gate has not passed.
- ⬜ **Not started** — no implementation should be inferred.
- ⛔ **Blocked / intentionally unsupported** — the exact technical or product reason is recorded.

Only ✅ items count as complete. Compiling code alone is not sufficient evidence for a rendering-capability task.

## Status summary

- Repository architecture, EasyGL responsibilities, platform GL ownership, renderer selection, resource interfaces, effect flow, test infrastructure, and upstream rlgl 6.0 have been audited.
- No RLGL implementation exists yet. Every renderer capability in the matrix remains unimplemented until its task-specific validation passes.
- The first vertical slice is selection → CNA-created GL context → rlgl loader/init → `GraphicsDevice` → clear → present → shutdown.
- Classic XNA 4.0 parity is the priority. Existing CNA extensions are tracked separately and do not gate the classic parity declaration.

## Current limitations

1. There is not yet a selectable `RLGL` renderer or an RLGL dependency in the build.
2. No device, resource, draw, effect, render-target, reset, or query operation is implemented.
3. Upstream rlgl keeps one process-global `RLGL` state object. The initial backend will therefore enforce one live RLGL device and will not claim concurrent multi-context support.
4. Several GL 3.3 operations required for XNA fidelity have no rlgl 6.0 public wrapper: arbitrary primitive modes and 32-bit index draws, depth comparison, stencil configuration, depth bias, indexed color masks, and some multisample attachment operations. Renderer-private bridge calls through the GL dispatch loaded by rlgl are permitted only for such measured gaps; they do not create CNA public API.
5. OpenGL 3.3 cannot provide every modern CNA extension (for example GL 4.3 compute/SSBO/image-load-store facilities). These are not classic XNA parity blockers.

## 1. Mission

The RLGL renderer is a CNA graphics backend that implements `CNA::Internal::Renderers::Common::IGraphicsRenderer` and its resource interfaces using standalone upstream rlgl. CNA remains the framework and owns XNA behavior, resource objects, effects, the game loop, windowing, input, audio, and platform services.

The verified architecture is:

```text
Microsoft::Xna::Framework graphics API
  -> GraphicsDevice / resources / effects
  -> CNA IGraphicsRenderer contracts
  -> CNA RLGL renderer and renderer-owned resource objects
  -> standalone rlgl low-level resource/state wrappers
  -> OpenGL 3.3 core
  -> CNA IPlatformGlContext
  -> selected CNA platform implementation (SDL2 or SDL3 today)
  -> platform OpenGL implementation
```

It is explicitly not:

```text
CNA -> raylib application framework -> rlgl
```

The backend will not initialize raylib, create a raylib window, use raylib input/audio/game-loop/resource APIs, or route any public XNA API through raylib types. Low-level rlgl APIs are preferred over rlgl's immediate-mode default batch because CNA already owns draw ordering, resources, explicit pipeline state, and effect semantics.

No new CNAEXT graphics API will be introduced to accommodate RLGL.

## 2. Dependency strategy

### Decision

Use CMake `FetchContent` to acquire the official `raysan5/raylib` repository at the exact raylib 6.0 commit `dbc56a87da87d973a9c5baa4e7438a9d20121d28`, but expose and compile only the standalone `src/rlgl.h` implementation and its required `src/external` loader headers. Do not configure, build, or link the raylib library.

This follows CNA's existing pinned-source dependency pattern used by Sokol and PortableGL while keeping the dependency surface substantially smaller than the full framework. An offline source override will be provided through `FETCHCONTENT_SOURCE_DIR_RLGL`, consistent with CMake's standard FetchContent convention.

### Recorded dependency facts

| Item | Decision / evidence |
|---|---|
| Upstream project | `https://github.com/raysan5/raylib` |
| Revision policy | Exact immutable commit for the selected stable release; never a moving branch |
| Initial revision | raylib 6.0, `dbc56a87da87d973a9c5baa4e7438a9d20121d28` |
| License | zlib/libpng license; preserve the upstream notice and record it in CNA third-party documentation |
| Used source | Standalone `src/rlgl.h`; one CNA-owned implementation translation unit defines `RLGL_IMPLEMENTATION` |
| Excluded source | raylib windowing, core loop, input, audio, model, image, and application/resource layers |
| GL loader | rlgl's bundled GLAD implementation for desktop GL 3.3; call `rlLoadExtensions()` with CNA's platform proc-address loader after making the context current |
| Platform libraries | Existing CNA platform/OpenGL context dependencies; no new native window library |
| Update procedure | Change the pinned commit, review `rlgl.h`, loader and license diffs, build the RLGL configuration, run contract/unit/parity/smoke suites, then update this plan and dependency documentation |

Vendoring a copied header would make upstream provenance and updates less visible. A system raylib package is rejected because it is not reproducible, often exposes only the full framework library, and may be compiled for an incompatible graphics profile. Linking full raylib is rejected because standalone rlgl is sufficient.

## 3. GL context ownership

The repository already has an appropriate GL ownership boundary in `IPlatformGlContext`, `PlatformGlContextOwner`, and `PlatformGlSurfaceState`. RLGL will reuse it.

| Concern | Owner | RLGL responsibility |
|---|---|---|
| Window creation/destruction | CNA platform/window service | Request an OpenGL-capable descriptor; never create a raylib window |
| GL context creation/destruction | CNA `IPlatformGlContext`, held through `PlatformGlContextOwner` | Request GL 3.3 core attributes, bind for rlgl init/use, release through CNA RAII |
| Context switching | CNA GL context service and renderer thread-context lease | Keep rendering calls on the owning thread/context; add explicit lease/rebind coverage before claiming reset or multithread support |
| Proc-address loading | CNA platform GL service | Pass the CNA loader to `rlLoadExtensions()` |
| Buffer swap | CNA GL context service | `Present()` flushes pending work and calls `SwapBuffers()` |
| Drawable/backbuffer size | CNA window surface plus `PlatformGlSurfaceState` | Re-query the drawable extent, update GL viewport and projection-dependent state on resize |
| Resize events | CNA window/event infrastructure | React through existing renderer/presentation entry points; do not install raylib callbacks |
| Vsync | CNA presentation interval / requested swap interval | Call the platform context's `SetSwapInterval()` |
| Fullscreen | CNA window service | Use existing `GraphicsDevice` presentation-mode flow; rlgl has no ownership role |
| Backbuffer MSAA | CNA renderer creation arguments plus platform context attributes | Request compatible sample attributes and report the achieved value; later validate resolve behavior |

rlgl initialization must occur only after a current context and successful extension load. Shutdown must make the same context current, release all renderer-owned resources, call `rlglClose()`, and then destroy the CNA-owned context.

Because rlgl 6.0 stores its state in a single static global object, the initial implementation will guard against a second live RLGL device. Supporting simultaneous RLGL devices would require upstream isolation work and is not implied by CNA's multi-renderer compile mode.

## 4. Supported rlgl profiles

Upstream rlgl 6.0 has compile-time paths for OpenGL software, OpenGL 1.1, 2.1, 3.3 core, 4.3 core, OpenGL ES 2, and OpenGL ES 3. The public CNA renderer identity remains one `RLGL` identity; profiles are an implementation/build choice rather than separate renderer identities.

| rlgl profile | Assessment | Initial decision |
|---|---|---|
| OpenGL 1.1 | Fixed pipeline; lacks programmable shaders, FBOs, modern buffers, and instancing needed by EasyGL/XNA workloads | Not supported |
| OpenGL 2.1 | Programmable but materially limits VAO, instancing, MRT, integer attributes, and framebuffer behavior | Deferred; not a parity target |
| OpenGL 3.3 core | Best match to CNA's existing desktop GL baseline; supports shaders, VAOs, FBOs, MRT, instancing, and broad texture/state coverage | **Selected initial profile** |
| OpenGL 4.3 core | Adds compute, SSBOs, image operations, and broader modern functionality, mostly CNAEXT rather than classic XNA | Future optional profile after GL 3.3 parity |
| OpenGL ES 2 | Too constrained for the first EasyGL parity campaign | Deferred |
| OpenGL ES 3 | Plausible portability target, but shader versions, readback, texture formats, state, and platform context behavior require a separate measured campaign | Future optional profile |

No public `RLGL33`, `RLGL43`, or `RLGLES` renderer identities will be created. A build-time profile option may be added only when a second profile has real implementation and validation.

## 5. EasyGL capability matrix

This matrix records measured implementation shape, not promises. “Direct GL bridge” means a renderer-private helper in the same translation unit as standalone rlgl, using the GL dispatch initialized by rlgl only where rlgl lacks a required public wrapper.

| Area | Capability | EasyGL | Upstream rlgl 6.0 / GL 3.3 | RLGL status | Classification / next evidence |
|---|---|---|---|---|---|
| Device | Initialization | Implemented | `rlLoadExtensions`, `rlglInit` | ⬜ | Straightforward implementation; vertical-slice smoke |
| Device | Resize | Implemented | Viewport/matrix APIs; CNA owns drawable size | ⬜ | Straightforward implementation; resize pixel test |
| Device | Fullscreen | Implemented through CNA platform | No window API needed | ⬜ | Straightforward integration; platform smoke |
| Device | Vsync | Implemented through platform context | No rlgl ownership | ⬜ | Straightforward integration; swap-interval verification |
| Device | Clear | Color/depth/stencil and selective planes | Public helper clears color+depth and omits stencil; exact selective clear needs bridge | ⬜ | Requires architectural work; clear-state restoration tests |
| Device | Present | Implemented | Flush plus CNA context swap | ⬜ | Straightforward implementation; visible/offscreen smoke |
| Device | Viewport | Implemented | `rlViewport` | ⬜ | Straightforward implementation; parity test |
| Device | Scissor | Implemented | `rlEnableScissorTest`, `rlScissor` | ⬜ | Straightforward implementation; clipping pixels |
| Device | Backbuffer formats | Negotiated/reported | GL framebuffer plus CNA context attributes | ⬜ | Requires architectural work; achieved-format reporting |
| Device | MSAA | Implemented | rlgl enables context MSAA and exposes framebuffer blit; exact attachment control is incomplete | ⬜ | Requires architectural work; sample/resolve pixels |
| Textures | Texture2D | Implemented | `rlLoadTexture`, update, bind, unload | ⬜ | Straightforward implementation; format/upload tests |
| Textures | TextureCube | Implemented | `rlLoadTextureCubemap`, cubemap binding and FBO faces | ⬜ | Straightforward implementation; six-face sample |
| Textures | Mipmaps | Implemented | `rlGenTextureMipmaps` | ⬜ | Straightforward implementation; level/filter validation |
| Textures | Texture updates | Implemented | `rlUpdateTexture` supports subrect upload | ⬜ | Straightforward implementation; partial update pixels |
| Textures | Readback | Implemented | Desktop `rlReadTexturePixels`; compressed readback unavailable | ⬜ | Requires architectural work; orientation/format tests |
| Textures | Filtering | Implemented | `rlTextureParameters` filter modes | ⬜ | Straightforward implementation; sampling parity |
| Textures | Address modes | Implemented | Wrap parameters | ⬜ | Straightforward implementation; U/V/W mapping tests |
| Textures | Anisotropy | Implemented when supported | Extension-gated anisotropic parameter | ⬜ | Straightforward implementation; capability-clamp test |
| Textures | Compressed formats | Broad platform-dependent support | DXT, ETC, PVRTC, ASTC paths gated by detected extensions | ⬜ | Requires architectural work; per-format capability/tests |
| Render targets | RenderTarget2D | Implemented | FBO create/attach/check/unload | ⬜ | Straightforward implementation; render/readback test |
| Render targets | MRT | Implemented | `rlActiveDrawBuffers`, maximum eight | ⬜ | Straightforward implementation; multi-attachment shader test |
| Render targets | Depth/stencil | Implemented | Depth/stencil attachment enums; detailed state needs bridge | ⬜ | Requires architectural work; depth/stencil pixels |
| Render targets | MSAA resolve | Implemented | FBO blit wrapper, incomplete multisample construction wrappers | ⬜ | Requires architectural work; explicit resolve test |
| Render targets | Switching | Implemented | FBO enable/disable | ⬜ | Straightforward implementation; transition/state tests |
| Render targets | Readback | Implemented | FBO/read-pixel paths; format/orientation conversion needed | ⬜ | Requires architectural work; subrect tests |
| Buffers | Vertex buffers | Implemented | VBO load/update/unload | ⬜ | Straightforward implementation; static draw |
| Buffers | Dynamic vertex buffers | Implemented | Dynamic upload flag and subrange update | ⬜ | Straightforward implementation; discard/no-overwrite semantics audit |
| Buffers | 16-bit index buffers | Implemented | EBO wrappers and unsigned-short indexed draw | ⬜ | Straightforward implementation; indexed draw |
| Buffers | 32-bit index buffers | Implemented | Upload works; draw wrapper hardcodes unsigned short | ⬜ | Underlying rlgl limitation; bridge draw plus test |
| Buffers | Dynamic index buffers | Implemented | Dynamic upload/update available | ⬜ | Straightforward implementation; update draw |
| Buffers | Buffer updates | Implemented | Whole/subrange update wrappers | ⬜ | Requires architectural work for XNA `SetDataOptions` semantics |
| Buffers | Primitive topology | Points, lines, triangle list/strip | Low-level draw wrappers hardcode triangles | ⬜ | Underlying rlgl limitation; bridge arbitrary GL mode |
| Buffers | Vertex declarations | Implemented semantic/type mapping | Generic attributes and divisors; CNA must map declarations | ⬜ | Requires architectural work; all declaration formats |
| Buffers | User primitives | Implemented | VBO or transient upload; default batch is semantically unsuitable | ⬜ | Requires architectural work; transient-ring design/test |
| Buffers | Indexed user primitives | Implemented | Same plus index-type limitation | ⬜ | Requires architectural work; 16/32-bit tests |
| Buffers | Instancing | Implemented | VAO attribute divisors and instanced draw wrappers | ⬜ | Requires architectural work; frequency/semantic tests |
| Pipeline | BlendState | Full XNA mapping | Blend enable plus combined/separate factors/equations | ⬜ | Straightforward implementation; pixel equations |
| Pipeline | DepthStencilState | Full XNA depth/stencil mapping | Depth enable/write exposed; compare and stencil ops have no public wrappers | ⬜ | Underlying rlgl limitation; bridge and exhaustive state tests |
| Pipeline | RasterizerState | Cull/fill/scissor/depth bias | Cull/scissor/wire exposed; exact cull selection and bias need care/bridge | ⬜ | Requires architectural work; rasterizer parity |
| Pipeline | SamplerState | Full XNA mapping | Texture parameters expose core filter/wrap/aniso | ⬜ | Straightforward implementation; per-slot state tests |
| Pipeline | Color write masks | Per-target masks | Global color mask wrapper only | ⬜ | Underlying rlgl limitation; indexed bridge where GL 3.3 permits |
| Pipeline | Culling | Clockwise/counterclockwise/none | Enable/disable and face selection wrappers | ⬜ | Straightforward implementation; winding convention test |
| Pipeline | Fill modes | Solid/wireframe | Wire/point helpers on desktop | ⬜ | Straightforward implementation; wireframe output |
| Pipeline | Depth bias | DepthBias/SlopeScaleDepthBias | No public rlgl wrapper | ⬜ | Underlying rlgl limitation; polygon-offset bridge/test |
| Pipeline | Stencil operations | Full front/back XNA stencil | No public rlgl wrapper | ⬜ | Underlying rlgl limitation; bridge/test front and back |
| Pipeline | Separate blending | Required by XNA state | Separate factor/equation wrappers exist | ⬜ | Straightforward implementation; color/alpha pixel tests |
| Shaders | BasicEffect | Implemented | Shader compile/link/uniform/attribute wrappers are sufficient | ⬜ | Requires architectural work; reuse `GpuDrawParams` and golden outputs |
| Shaders | AlphaTestEffect | Implemented | Same | ⬜ | Requires architectural work; alpha-function matrix |
| Shaders | DualTextureEffect | Implemented | Multiple texture/sampler binding available | ⬜ | Requires architectural work; texture-combine pixels |
| Shaders | EnvironmentMapEffect | Implemented | Cubemap and shader support available | ⬜ | Requires architectural work; reflection/fog/lighting |
| Shaders | SkinnedEffect | Implemented | Uniform arrays available, limits must be measured | ⬜ | Requires architectural work; bone-limit/skinning parity |
| Shaders | Other built-in effect families | Implemented where CNA exposes them | General shader primitives available | ⬜ | Requires architectural work; inventory from EasyGL hooks |
| Shaders | Custom compiled Effect | Optional MojoShader path | rlgl can host generated GLSL, but versions/reflection/samplers need adaptation | ⬜ | Requires architectural work; compile/apply/pass tests |
| Shaders | Shader constants/uniforms | Implemented | Scalar/vector/matrix/array setters | ⬜ | Straightforward primitives; type/layout regression tests |
| Shaders | Matrices | XNA conventions normalized by renderer shaders | Column-major rlgl matrix representation; raw uniform upload available | ⬜ | Requires architectural work; world/view/projection probes |
| Shaders | Textures/samplers | Implemented | Active texture, cubemap, sampler parameters | ⬜ | Straightforward primitives; slot/reflection tests |
| Shaders | Vertex attributes | Semantic declaration mapping | Generic attributes plus integer/divisor variants | ⬜ | Requires architectural work; semantic/type inventory |
| Shaders | Fog | Implemented in stock effects | General shader support | ⬜ | Requires architectural work; linear fog golden tests |
| Shaders | Lighting | Implemented in stock effects | General shader support | ⬜ | Requires architectural work; per-light golden tests |
| Shaders | Skinning | Implemented | General shader support subject to uniform limits | ⬜ | Requires architectural work; max bones and blend channels |
| 2D | SpriteBatch | Implemented through renderer sprite-batch interface | High-level rlgl batch cannot preserve all CNA state/sort semantics; low-level buffers can | ⬜ | Requires architectural work; implement CNA-owned batching |
| 2D | SpriteFont | Implemented through SpriteBatch | Texture/quad primitives sufficient | ⬜ | Requires architectural work; text golden images |
| 2D | Texture drawing | Implemented | Texture/shader/buffer primitives sufficient | ⬜ | Requires architectural work; source/destination/origin tests |
| 2D | Rotation/scaling | Implemented | Shader/vertex math sufficient | ⬜ | Straightforward after SpriteBatch; transform tests |
| 2D | Sorting | Implemented XNA sort modes | Must remain CNA-owned, not rlgl batch-owned | ⬜ | Requires architectural work; order/blend tests |
| 2D | Blending | Implemented | Blend APIs sufficient | ⬜ | Straightforward after state layer; pixel tests |
| 2D | Clipping | Implemented | Scissor APIs sufficient | ⬜ | Straightforward after state layer; rectangle pixels |
| 3D | Meshes | Implemented through Model/Mesh draw paths | General indexed vertex draws available with bridge gaps | ⬜ | Requires architectural work; sample corpus |
| 3D | Models | Implemented | Renderer consumes CNA model resources, not raylib models | ⬜ | Requires architectural work; existing model samples |
| 3D | Lighting | Implemented | Shader capability sufficient | ⬜ | Requires architectural work; stock-effect parity |
| 3D | Skinning | Implemented | Shader/vertex capability sufficient within limits | ⬜ | Requires architectural work; animated model sample |
| 3D | Render targets | Implemented | FBO support available | ⬜ | Requires architectural work; multipass sample |
| 3D | Custom shaders | Implemented where MojoShader is enabled | Low-level shader APIs available | ⬜ | Requires architectural work; existing compiled-effect samples |
| 3D | Instancing | Implemented | Instanced draw and divisor APIs available | ⬜ | Requires architectural work; existing instancing samples |
| Other | Occlusion queries | Implemented | No rlgl public query wrappers; GL 3.3 provides query objects | ⬜ | Underlying rlgl limitation; renderer-private bridge/test |
| Other | Capabilities reporting | Detailed renderer limits/features | Extension flags are internal and GL limit wrappers are partial | ⬜ | Requires architectural work; bridge queried limits and truthful flags |
| Other | GraphicsAdapter | Descriptor-driven enumeration and support probes | Can use generic CNA adapter reporting before a live context | ⬜ | Straightforward baseline; live limits remain device-specific |
| Other | Resource disposal | Deterministic and context-aware | Unload wrappers available; require current context | ⬜ | Requires architectural work; idempotence/use-after-dispose tests |
| Other | Device reset/loss | EasyGL has recovery infrastructure | rlgl global state and default-resource recreation require coordinated reinit | ⬜ | Requires architectural work; loss/restore/resource-shadow tests |
| Other | Debug validation | Error/debug checks and diagnostics | `rlCheckErrors`, debug callback support depends on profile/extensions | ⬜ | Straightforward baseline, deeper validation later |
| Other | Threading assumptions | Context lease and renderer synchronization | rlgl global state is not thread-safe | ⬜ | Requires architectural work; owning-thread contract and lease tests |
| CNAEXT | Texture3D and modern extras | EasyGL supports selected extensions | No public rlgl Texture3D wrapper; GL 3.3 can support some through a bridge | ⛔ | Not part of classic XNA parity; defer until the classic gate |
| CNAEXT | Compute/SSBO/image operations | EasyGL/profile-dependent | rlgl exposes these only for GL 4.3 | ⛔ | Underlying GL-profile limitation; intentionally deferred |

## 6. Shader and effect strategy

RLGL will not introduce a public shader language or effect API. The renderer must consume the same CNA `GpuDrawParams`, stock-effect inputs, `ShaderEffect`, and optional compiled-effect flow used by EasyGL.

The first shader task must inventory EasyGL's stock shader variants and the semantic-to-location, uniform, sampler, matrix, clip/depth, texture-orientation, color, fog, lighting, and skinning conventions. The GL 3.3 implementation will compile renderer-private GLSL 330 variants or reuse compatible shared sources where they actually match. Custom Effect support must reuse CNA's existing representation/translation path rather than accepting RLGL-specific sources.

Regression tests will isolate conventions: matrix layout, half-pixel behavior if applicable, clip-space depth, render-target texture orientation, vertex color normalization, integer attributes, sampler slots, fog endpoints, alpha test boundaries, and bone transforms. Shader variants will not be patched per sample.

## 7. State correctness design

The renderer will use rlgl's low-level resource and draw wrappers, not its default immediate/batch path, for CNA rendering. The default batch's draw records are texture-oriented; program, matrix, VAO, and some state changes force flushes and it unbinds/reset selected GL state after submission. Mixing that machinery with CNA's explicit state model would make state ownership ambiguous.

RLGLRenderer will own a deterministic state cache for program, textures, samplers, blend, depth, stencil, rasterizer, scissor, viewport, framebuffer, vertex/index buffers, and attribute arrays. Cache entries are invalidated after any operation that can mutate or reset state outside the renderer's controlled path. Renderer-private GL bridge operations must be centralized, documented by the missing rlgl wrapper they replace, and followed by the same cache rules.

Every state-family task must test both isolated application and transitions A → B → A. Render-target switches, clears, readbacks, effect passes, and context restoration must preserve or explicitly reapply XNA-visible state.

## 8. Implementation DAG and task ledger

| ID | State | Depends on | Task and completion evidence |
|---|---|---|---|
| RLGL-001 | ✅ | — | Audit renderer selection, common contracts, GraphicsDevice/Adapter, presentation, platform GL ownership, resources, effects, SpriteBatch, lifetime/reset, diagnostics, build/CI, renderer tests, parity infrastructure, and comparable renderer plans. Evidence is recorded in Sections 1–8. |
| RLGL-002 | ✅ | — | Audit upstream rlgl 6.0 profiles, configuration, loader, lifecycle, resource/state/shader/FBO/draw APIs, batch behavior, formats, limits, and restrictions. Evidence is recorded in Sections 2–7 and the matrix. |
| RLGL-003 | ✅ | RLGL-001, RLGL-002 | Decide the dependency, initial GL profile, context ownership, renderer identity, low-level strategy, singleton policy, and private bridge boundary. Decisions are recorded above. |
| RLGL-004 | ✅ | RLGL-001, RLGL-002 | Establish the measured EasyGL/rlgl/RLGL capability matrix and seed the concrete implementation DAG. Matrix reviewed against the public/common renderer contracts. |
| RLGL-005 | ⬜ | RLGL-003 | Add pinned standalone-rlgl CMake acquisition, renderer identity/registry/descriptor integration, explicit C/C++ ABI identity mappings, combination rules, and identity consistency checks. Validate configure/build and identity scripts. |
| RLGL-006 | ⬜ | RLGL-005 | Implement descriptor and minimal renderer: CNA GL 3.3 core context, one-live-device guard, rlgl loader/init, drawable tracking, clear, present, viewport, presentation interval, and orderly shutdown. Validate a complete build. |
| RLGL-007 | ⬜ | RLGL-006 | Add automated or runnable context smoke coverage for device creation, clear/present, framebuffer readback where the environment permits, resize, and clean destruction. Record headless/Xvfb requirements. |
| RLGL-008 | ⬜ | RLGL-006 | Document enablement, dependency/offline override, supported initial platforms/profile, debugging, licensing, and current limitations; add configure/compile CI coverage without overstating runtime coverage. |
| RLGL-009 | ⬜ | RLGL-006 | Implement Texture2D formats, upload/sub-update/readback/mipmaps and SamplerState with truthful capability reporting and format/sampling tests. |
| RLGL-010 | ⬜ | RLGL-009 | Implement a CNA-owned low-level SpriteBatch path, then validate texture drawing, transforms, origin/effects, sorting, blending, clipping, and SpriteFont with existing tests/samples. |
| RLGL-011 | ⬜ | RLGL-006 | Implement vertex declarations, static/dynamic vertex and 16/32-bit index buffers, user primitives, all XNA topologies, update options, and draw calls. Validate format/topology/update matrices. |
| RLGL-012 | ⬜ | RLGL-009, RLGL-011 | Implement shader infrastructure and stock BasicEffect, AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, and other classic effect hooks found by the final inventory. Validate semantic regressions and golden output. |
| RLGL-013 | ⬜ | RLGL-009, RLGL-011 | Implement BlendState, DepthStencilState, RasterizerState, deterministic cache/invalidation, selective clear restoration, and transition tests including private bridge operations. |
| RLGL-014 | ⬜ | RLGL-009, RLGL-013 | Implement RenderTarget2D, MRT, depth/stencil attachments, switching, readback, MSAA construction/resolve, and content preservation/discard rules. Validate multipass pixels. |
| RLGL-015 | ⬜ | RLGL-009, RLGL-012, RLGL-014 | Implement TextureCube, cube render targets, compressed formats, broader readback/mipmap behavior, and 3D/model workload prerequisites. |
| RLGL-016 | ⬜ | RLGL-011, RLGL-012, RLGL-013 | Implement instancing and occlusion queries using rlgl wrappers where present and the documented bridge where absent. Validate divisors, instance frequency, query availability/result/lifetime. |
| RLGL-017 | ⬜ | RLGL-009–RLGL-016 | Implement disposal robustness, owning-thread/context leases, diagnostics, achieved capabilities, context loss/reset, and resource restoration. Validate failure injection and use-after-dispose behavior. |
| RLGL-018 | ⬜ | RLGL-010–RLGL-017 | Run the representative workload ladder: clear/present, textured triangle, indexed geometry, SpriteBatch/Font, multiple textures, render targets, depth 3D, AlphaTest/BasicEffect, lighting, fog, skinning, dynamic buffers, instancing, state switching, and multipass rendering. Add only missing focused probes. |
| RLGL-019 | ⬜ | RLGL-018 | Execute the systematic EasyGL classic-XNA parity campaign using shared/unit/parity tests and golden images; record each observable comparison and append every discovered gap as a concrete task. |
| RLGL-020 | ⬜ | RLGL-018 | Measure backend overhead after correctness: state/program/texture binds, uploads, framebuffer transitions, flushes, per-draw allocations, conversions, and lookups. Optimize only measured regressions and retain semantics. |
| RLGL-021 | ⬜ | RLGL-019 | Complete CI and documentation for supported platforms, runtime GL smoke availability, update procedure, debugging, licensing, parity status, and verified limitations. |
| RLGL-022 | ⬜ | RLGL-019–RLGL-021 | Final audit: rescan EasyGL and RLGL hooks/resources/state/errors/effects/tests/CI/docs, search TODO/stub/fallback paths, rerun representative suites, reconcile this matrix, and create tasks for every new classic-XNA gap. Only this task may declare parity. |

Tasks discovered while implementing will be appended with stable identifiers; existing identifiers will not be renumbered.

## 9. Validation strategy

Validation proceeds in increasing cost:

1. Renderer identity and descriptor consistency scripts.
2. RLGL configuration and compile-only build.
3. Unit tests that do not require a graphics context.
4. Runtime smoke under a real or virtual GL 3.3 context.
5. Focused resource/state/effect tests with readback.
6. Existing shared parity corpus run under both EasyGL and RLGL.
7. Existing representative samples, preferring repository workloads over parallel applications.
8. Golden-image or pixel comparisons for output-sensitive behavior.
9. Cross-renderer regression builds so RLGL changes do not break established renderers.

Every completed implementation task records the exact commands and result in its task entry or a dated evidence note. Unsupported paths must return truthful capability data or explicit errors; a no-op success is never acceptable.

## 10. CI, documentation, and performance gates

CI should always configure and compile the RLGL backend. Runtime coverage is added only on workers with a proven GL 3.3 context, using the same headless/offscreen mechanism already accepted by CNA where possible. The default and at least one established GL renderer must continue to build after shared changes.

Documentation must explain `-DCNA_GRAPHICS_RENDERER=RLGL`, the exact pinned source and offline override, OpenGL 3.3/core requirements, supported platform/context providers, current parity backed by this matrix, debugging paths, license notices, and the dependency update procedure.

Performance work begins only after the representative correctness gate. Measurements must identify redundant state transitions, binds, uploads, framebuffer changes, flushes, conversions, allocations, or lookup overhead with a reproducible workload. No optimization may weaken XNA semantics.

## 11. Final parity gate

“RLGL ↔ EasyGL classic XNA parity reached” may be stated only when RLGL-022 completes with:

- every classic-XNA matrix row either validated as supported or documented as an unavoidable rlgl/GL-profile limitation with an explicit observable behavior;
- the EasyGL and RLGL contract inventories reconciled;
- representative workloads and shared parity tests passing on supported platforms;
- no hidden stub, fallback, silent no-op, stale-state, resource-lifetime, or shader/effect gap;
- CI/build integration and user/update/debug/license documentation verified;
- CNAEXT gaps separately listed and not confused with the classic-XNA result.

Until then, the authoritative status is the summary, limitations, matrix, and task ledger above.
