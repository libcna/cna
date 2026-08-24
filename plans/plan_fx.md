# Compiled XNA Effect Bytecode Support Plan

- Status: **Four backends supported and gated: FNA3D, SDL_GPU, the EasyGL/OpenGL family and
  Vulkan. The FNA3D vertical slice was declared usable (`FX-057`) on 2026-08-15; a repair pass on
  2026-08-17 (`FX-080`–`FX-090`) closed the silent-fallback and coverage gaps the first rollout left
  behind and turned the `FX-060` shared suite into a real capability gate with a read-back draw
  matrix; a follow-up pass on 2026-08-18 (`FX-091`–`FX-110`, Phase J) did the same for the
  texture/sampler/pass half, which the first pass had left asserted against CNA's own state objects;
  a closure pass the same day (`FX-111`–`FX-112`, Phase K) finished Vulkan and fixed the shared
  suite's own gating. `CompiledEffects` is true on all four and is justified by drawing rather than
  by state inspection. Every other renderer identity is `CompiledEffects == false` and refuses by
  name (section 10.2/10.3). Section 10.5 classifies every remaining limitation as renderer-wide or
  compiled-Effect-specific. The project-wide Definition of Done (section 10.2) is **not** met:
  DirectX 11, DirectX 9 and Metal remain, and none of the three can be built or executed on the
  Linux development machine this work happens on**
- Planning baseline: `develop` at `a749fdce34a5825eb80a778b5db68e11da9358f8`
- Target branch: `feature/fx`
- Scope of this document: architecture, implementation checklist, and current delivery status

## Implementation snapshot (2026-08-14, amended 2026-08-17)

> Read the repair-pass section below this one first if you are picking the feature up. This snapshot
> is accurate for what the FNA3D slice delivered; several claims it makes about the SDL_GPU and
> EasyGL rollout were true of the routes those backends had implemented and not of the ones they had
> not, which is what `FX-080`-`FX-090` corrected.


The first production backend is now implemented, not merely parser-prototyped. FNA3D owns the
native MojoShader effect; the common graphics layer owns the reflected XNA object graph and mutable
parameter storage. Direct construction and the canonical XNB reader both work, compiled passes are
preserved through general primitive and SpriteBatch draws, and unsupported renderers retain an
explicit `CompiledEffects == false` boundary.

Delivered task groups:

- format boundary/sniffing and provenance-pinned stock fixtures (`FX-001`, `FX-003`, `FX-006`);
- renderer-neutral runtime, capability, draw token, concrete base `Effect`, reflection object graph,
  padded values, exact pass identity, dirty upload, clone, and active-effect disposal
  (`FX-010`–`FX-021`, `FX-024`);
- FNA3D creation/reflection/value upload/clone/technique/pass, render and sampler translation,
  draw preservation, truthful capability, 3D/SpriteBatch pixels, malformed-input and lifecycle
  coverage (`FX-031`–`FX-038`, with sanitizer/reset coverage still open);
- an in-tree, legally reproducible Effect Framework 9.1 conformance binary builder covering
  defaults, scalar/vector/matrix/array reflection, annotations, two techniques, three exact passes,
  all supported FNA render-state tokens, the unknown-token policy, clone isolation, and a
  blend-factor pixel oracle (partial `FX-004`, `FX-022`, and `FX-037`);
- pass state published through the public `GraphicsDevice` state objects and sampler/texture
  collections exactly as FNA's `Effect.cs` does, instead of into renderer-private caches, with a
  group the pass never assigned left as the game selected it (`FX-022`, `FX-024`);
- a performance baseline for construction, clone, dirty upload, clean apply and draw, with the
  immutable-artifact-cache question decided against on those numbers (`FX-053`);
- a full-suite regression run under FNA3D with every remaining failure explained, which caught and
  fixed one real branch regression (FNA3D's instancing refusal message had moved when compiled
  effects unlocked instancing, and its test still expected the old wording) and surfaced a
  pre-existing FNA3D device-lifetime use-after-free now recorded in `known_bugs.md` (`FX-054`);
- a renderer-neutral conformance suite under `tests/support/CNA/TestSupport/` that a new backend
  runs with only its own device setup, with FNA3D as its first consumer (`FX-060`);
- porter-facing documentation covering the whole compiled path, its error table, and the
  dependency/licence notices, plus a fuzzing guide (`FX-055`, `docs/fx-compiled-effects.md`,
  `docs/fx-bytecode-fuzzing.md`);
- a libFuzzer/AFL++ entry point covering construction, reflection, clone, technique/pass
  application and post-source-destruction clone use, which also builds as a standalone corpus
  replayer and mutation-campaign driver in any configuration, plus the deterministic in-build
  mutation corpus extended from construction only to that same full surface (`FX-051`);
- forty upstream crash classes found by that campaign and fixed in the managed MojoShader patch --
  a dereferenced NULL preshader parse, a SPIR-V attribute fixup assert, register copies sized by
  the constant table rather than the parsed storage, an unbounded preshader operand count, two
  asserts on untrusted preshader tokens, an allocation sized before its own bounds check, and an
  unchecked shader-array selector index, a preshader interpreter whose every index was guarded
  only by asserts, register copies unbounded against the register file they write into, and a
  pass's shader object index used without a range or type check on a union -- with the remaining
  exposure recorded and reproducible rather than hidden (`FX-050`, `FX-051`);
- one crash class of CNA's own, found by the same campaign after 1,627,248 executions: the
  sampler-texture map selected parameters by sampler type alone, where the storage layout follows
  the class first and the type second, and read a small value buffer as an array of sampler states
  (`FX-051`);
- lifecycle coverage for device reset, out-of-order clone-chain disposal, disposed-effect
  rejection, repeated mid-construction native failure, and repeated create/apply/dispose cycles
  (`FX-038`);
- the same fixture builder extended with a hand-assembled Shader Model 2.0 program and its
  Direct3D 9 constant table, which is what makes MojoShader report sampler state registers, plus
  per-slot sampler conformance for addressing, LOD bias, mip level, anisotropy, the full
  filter-component collapse table, exact register targeting, reflected texture binding, clone
  isolation, and both rejection policies (`FX-023`, `FX-034`, more of `FX-004`);
- canonical bounded `EffectReader`, replacement of the unsupported placeholder, and negative
  payload tests, plus an end-to-end ContentManager/XNB/render pixel test (`FX-040`–`FX-043`);
- dedicated false-by-default capability gating for every non-FNA3D renderer and bounded common,
  native-reflection, pass-state and sampler-state graphs (`FX-036`, partial `FX-050`);
- a checked compiled-Effect preflight for the complete reflected value/object/technique/pass graph,
  including nested aggregate/storage sizing, bounded offsets and counts, object-record validation,
  and a 64 MiB reflected-storage ceiling (partial `FX-050`);
- a repository-managed patch for pinned MojoShader `6333f74dbd5644789a63e903816441b16c1e8b60`
  that turns a missing shader-to-Effect parameter match into an ordinary parser error. CMake applies
  the versioned patch automatically and idempotently for both normal FNA3D FetchContent checkouts
  and explicit source overrides (`FX-030`, partial `FX-050`).

The FNA3D state mapping has also been audited token-by-token against FNA's `Effect.cs`, including
its historical blend-factor byte order, separate-alpha propagation rules, boolean interpretation,
and the way anisotropic filter components collapse into the eight XNA aggregate filters. A focused
`BasicEffect.fxb` regression now reproduces the former missing-symbol assertion with seed
`0x46584241534943`, iteration 121, and byte mutation `offset 25018 xor 0x04`; it is rejected with a
normal MojoShader diagnostic. The deterministic malformed-input corpus covers 512 synthetic and
128 stock-Effect mutations without per-case output or a flaky wall-clock assertion, while retaining
the seed, iteration, size, and mutation description in failure traces.

A fresh ASan/UBSan build executes the FX/XNB/capability suites without an address-sanitizer or CNA
undefined-behavior finding after MojoShader enum fields were made safe to inspect even when mutated
bytes do not encode a valid C++ enum. The 2026-08-14 re-run over the current branch covers **340
tests** (`Fna3dCompiledEffect*`, `Effect*`, every XNB suite, capability and content-reader suites)
on the SDL_GPU/Vulkan driver: all pass, AddressSanitizer reports nothing, and every one of the 19
UBSan reports is in third-party code -- `SpirvPatchTable` alignment and null-argument reports in the
pinned MojoShader plus one `left shift of 255 by 24 places` in FNA3D's own pipeline cache. Zero
UBSan reports are attributable to CNA.

The earlier claim that LeakSanitizer cannot operate under this managed environment's ptrace policy
was **wrong and is corrected here**: LSan runs. It reports 209,008 bytes in 15,479 allocations
across the compiled-effect suite, and every one of the 1,600 leak records is allocated by
third-party code -- 1,562 by MojoShader's SPIR-V emitter (`spv_load_srcarg`,
`spv_add_attrib_fixup`, `spv_componentlist_alloc`, `emit_SPIRV_*`) and 37 by `FNA3D_CreateDevice`
itself, 32 bytes per device. **No leak record is allocated by CNA code.** These are recorded as
upstream findings rather than misrepresented as a clean third-party sanitizer gate. A broader affected debug run passes
1,203 Effect, SpriteBatch, GraphicsDevice, primitive, and model tests. A build directory created
from scratch fetched the exact pinned FNA3D and MojoShader revisions, applied only the managed
MojoShader patch, built `CnaTests`, and passed the focused regression, corpus, and all 58 targeted
tests; configuring the same checkout again confirmed that patch application is idempotent.

Still open before the FNA3D slice can satisfy every aspirational exit criterion in this plan:

- the pixel half of the FNA oracle (`FX-005`). Reflection and pass-state observations from FNA now
  both exist and pass; what is still missing is FNA-produced pixels, which depend on the GPU and
  driver rather than only on CNA's translation and are correspondingly less portable as a fixture;
- proof rather than a bound for hostile input (`FX-051`). The gate is met on both FNA3D drivers --
  over three million and over two and a half million coverage-guided executions with no finding --
  after forty-one crash-class fixes, but fuzzing cannot establish absence and the SPIR-V emitter
  still asserts on shader bytecode in places no campaign has reached;
- additional renderers consuming `SamplerState.AddressW` (`FX-026` carried it through the shared
  contract and FNA3D consumes it; the rest keep the documented no-op default);
- additional renderer implementations (`FX-061`–`FX-071`). SDL_GPU (`FX-061`, `FX-071`),
  EasyGL/OpenGL/OpenGL ES (`FX-062`) and Vulkan (`FX-064` prototype, `FX-065` renderer) are all
  **done** -- real draw routes through the public `Effect`/`GraphicsDevice` API, golden-pixel tests,
  `SupportsCompiledEffects()` true. The first two needed a repair pass afterwards
  (`FX-080`-`FX-090`, see the section above this one) before that claim was true of *every* draw
  route rather than the two each had implemented; Vulkan was written against the repaired contract
  and hit five of the same defect shapes in its own code on the way (see `FX-065`). Still open:
  DirectX 11 (`FX-063`, Windows-gated), Metal (`FX-066`) and DirectX 9 (`FX-070`, structurally the
  smallest of them because it consumes the effect's shader bytecode untranslated). None of those
  three can be verified on this Linux development machine. Every renderer identity is now classified
  as planned, assessed-feasible or unsupported-by-design in section 10.3 (`FX-067`). The shared
  contract a backend must pass exists (`FX-060`); until it passes, that backend's correct behavior
  is an explicit `NotSupportedException`, never a silent stock-shader fallback.

## Repair pass (2026-08-17): `FX-080`-`FX-090`

The first rollout enabled `CompiledEffects` on three backends. An audit of what those backends
actually did with a compiled effect found that several draw routes accepted one and then rendered
with something else, and that the shared suite could not have caught it because it contained no
draw at all. This pass fixed the routes and made the suite able to see the difference.

What was wrong, and is now fixed:

- **EasyGL's SpriteBatch silently used the stock sprite shader** for a compiled `Effect`.
  `FlushBatch()` resolved the program through `Effect::GetEffectRendererPtr()`, which returns null
  for a compiled effect, so `prog` stayed `&program_`; the effect was applied, its state published,
  and the batch drawn with a shader the game never asked for. There was no error and no clue.
  EasyGL now has a real compiled-effect sprite route (`FlushBatchWithCompiledEffect`), applying
  every pass of the current technique and overwriting slot 0 with the drawn texture afterwards,
  exactly as FNA's `SpriteBatch.DrawPrimitives` does (`FX-080`).
- **SDL_GPU's SpriteBatch route only worked if the caller had applied a pass by hand.** Nothing in
  `SpriteBatch.Begin(..., effect)` applied one, so the queued binding was captured from whatever
  pass happened to be applied last -- or from none. It now applies the passes itself, one queued
  sprite per pass (`FX-080`).
- **EasyGL's instanced draw ignored a compiled effect entirely.** `DrawInstancedPrimitivesEx` had
  no compiled branch, so it fell through to `SelectProgram()` and drew instanced geometry with a
  stock shader. It now dispatches like the other two routes, with per-attribute divisors taken from
  each stream's own `InstanceFrequency` (`FX-082`).
- **EasyGL's compiled draw read only the first vertex buffer.** It passed one declaration and one
  stride to the binder, so a shader consuming attributes from a second bound stream got them from
  the wrong buffer. The binder now resolves every shader input across the whole bound stream set and
  binds each from its own buffer, stride and `VertexOffset` (`FX-082`).
- **`DrawUserPrimitives`/`DrawUserIndexedPrimitives` staged buffers with no `VertexDeclaration`.**
  The typed overloads packed a built-in vertex type and uploaded it, leaving the renderer only a
  byte stride -- enough for the stock shader families, not for a compiled effect, whose vertex
  shader declares arbitrary semantics. `GraphicsDevice` now sets the vertex type's own canonical
  declaration on the staged buffer, in the renderer-neutral layer, exactly as FNA's
  `VertexDeclarationCache<T>` does (`FX-081`).
- **Effect sampler state stopped at the device state object.** `ApplySamplerMipState` was a
  default no-op on every renderer but FNA3D, SDL_GPU hard-coded `max_lod` and never set `min_lod`
  or `mip_lod_bias`, and EasyGL's compiled route applied no sampler state at all -- a bound
  texture's creation-time GL parameters filtered it instead of the pass's `sampler_state` block
  (`FX-083`).
- **A compiled draw into a render target was vertically mirrored against every other EasyGL draw.**
  The route reported `renderTargetBound = 1` to `MOJOSHADER_glProgramViewportInfo`, which negates
  `gl_Position.y` -- FNA3D's way of emulating Direct3D 9 top-down render targets, paired there with
  an inverted front face. EasyGL never flips geometry for an FBO; it corrects the bottom-up texel
  order where it is observed. The mismatch reversed winding and culled the SpriteBatch quad away
  entirely. Reported as unbound now, with the real target size still passed so `VPOS`'s own flip
  stays correct (`FX-088`).
- **`EffectParameter::SetValue(std::string)` threw `NotImplementedException`.** XNA's own
  implementation sets the value and rejects a non-`String` parameter with `InvalidCastException`;
  FNA leaves it unimplemented with a FIXME. CNA now follows XNA (`FX-089`).
- **`FX-060` could not detect any of the above.** The shared suite had nine sections and not one
  draw: a backend could pass it in full while every draw it issued used a stock shader. It now has
  six more sections -- parameter API, draw matrix, multi-stream, instancing, SpriteBatch,
  orientation and effect switching -- and each draw section reads a pixel back and compares it
  against the effect's own `Tint` parameter (`FX-084`-`FX-088`).

### Verification (2026-08-17)

All runs on a private Xvfb (`:90`), `CnaTests` launched from the repository root, `-j3` builds.

| Tree | Configuration | Result |
|---|---|---|
| `cmake-build-easygl` | `OPENGLES3`, `CNA_EASYGL_COMPILED_EFFECTS=ON` | 7392 passed, **0 failed** |
| `cmake-build-fna3d` | `FNA3D` | 7320 passed, 2 failed -- both the documented `GameEventSemanticsGoldenTest/Headless`+`/Terminal` gap |
| `cmake-build-sdlgpu` | `SDL_GPU`, `CNA_SDL_GPU_COMPILED_EFFECTS=ON` | 7290 passed, 5 failed -- the two documented `Cnj*` GLSL-dialect cases, the same two golden cases, and `RendererStrideConformance.EveryGltfStrideReachesTheNativeDrawBoundary` |
| `cmake-build-fna3d-asan` | `FNA3D`, `CNA_SANITIZE=address,undefined` | 279 FX/Effect/XNB tests passed; **zero AddressSanitizer findings**; all five UndefinedBehaviorSanitizer reports inside pinned MojoShader (`mojoshader_common.c`, `mojoshader_effects.c`), none attributable to CNA. A separate LeakSanitizer run over the six new draw contracts attributes every record to `libGLX_mesa.so`, none to CNA or MojoShader |

Excluded from the full runs, all pre-existing and recorded in the environment notes:
`TwoProcessLoopbackTest.*`, `CnaInputClipboardTest.*`,
`MetalResourceHealth.RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove`,
`GamePlatformOwnershipTest.*`, and `ENet*` -- the last because
`ENetDiscoveryServiceTest.ReplyToQueryOnlyAnswersWhenSessionTypeFilterMatchesTheHost` hangs
indefinitely on this machine.

`RendererStrideConformance` on SDL_GPU is not caused by this work:
`SdlGpuRenderer::DrawIndexedPrimitivesEx` falls through to `DrawColoredPrimitives` for a stride-24
unlit buffer, which then refuses anything but stride 16. It is a real SDL_GPU gap for unlit
vertex-coloured glTF geometry and wants its own task.

**Correction (2026-08-18, follow-up pass).** The sentence that used to stand here -- "No file on
that path is in this change set" -- was false: `SdlGpuRenderer.cpp` *is* in the `FX-080`-`FX-089`
change set. What is true, and is what the conclusion actually rests on, is narrower and checkable:
neither the stride-16 guard nor the dispatch that reaches it has been touched by any FX work.
`git log -S "DrawColoredPrimitives requires a stride-16" -- modules/renderers/sdl-gpu/src/SdlGpuRenderer.cpp`
returns exactly one commit, `57aee5f88` ("rename graphics backend terminology to renderer"), which
predates this feature branch's FX work entirely. The failure is pre-existing and unrelated; the
old wording overstated the evidence for that.

The four platform boundary gates were run too: `sdl_inventory --check` and `hot_path_lint` pass;
`sdl_classify --check` and `renderer_sdl_audit --check` fail identically on a stashed clean tree
(`SDL_WINDOWEVENT_*` symbols and the `pixijs` family), so both are pre-existing.

Found and fixed outside the FX scope, because it blocked verifying any of this on the reference
backend: **FNA3D could not create a device at all on this branch.** `plan_runtimerenderer.md`
RTR-P1-D41 replaced the descriptor's `prepareWindowFlags` hook with static data, which is right for
the window's visual -- but that hook held the only call to `FNA3D_PrepareWindowAttributes()` in the
production path, and that call is where FNA3D *selects its driver*. Every `FNA3D_CreateDevice`
failed with "Call FNA3D_PrepareWindowAttributes first!", so every FNA3D test failed at
`GraphicsDevice device;`. Restored in `Fna3dRenderer`'s constructor (`FX-090`).

### Verification (2026-08-18, follow-up pass)

All runs on a private Xvfb (`:77`), `CnaTests` launched from the repository root, `-j3` builds, each
tree rebuilt from the sources in this change set before it was run. The gtest filter excludes the
same set the previous pass documented -- `TwoProcessLoopbackTest.*`, `CnaInputClipboardTest.*`,
`MetalResourceHealth.RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove`,
`GamePlatformOwnershipTest.*` and `ENet*` -- the last because
`ENetDiscoveryServiceTest.ReplyToQueryOnlyAnswersWhenSessionTypeFilterMatchesTheHost` hangs
indefinitely on this machine (confirmed again this pass: an unfiltered run stopped there at 6082
tests and had to be killed).

| Tree | Configuration | Ran | Passed | Skipped | Failed |
|---|---|---:|---:|---:|---:|
| `cmake-build-fna3d` | `FNA3D` | 7516 | 7325 | 188 | 3 |
| `cmake-build-sdlgpu` | `SDL_GPU`, `CNA_SDL_GPU_COMPILED_EFFECTS=ON` | 7436 | 7299 | 131 | 6 |
| `cmake-build-easygl` | `OPENGLES3`, `CNA_EASYGL_COMPILED_EFFECTS=ON` | 7459 | 7398 | 60 | 1 |

Every failure classified, none introduced by this pass:

| Test | Trees | Classification |
|---|---|---|
| `TerminalRestoration.SighupGivesTheTerminalBack` | all three | **Harness artifact of this pass's own run method.** The suites were launched under `setsid nohup` so they would survive the 120-second foreground limit, which leaves the process with no controlling terminal. Re-running the case attached passes all seven `TerminalRestoration` cases. Not a product failure, and not present in the previous pass's numbers because that pass ran attached |
| `GameEventSemanticsGoldenTest.../Headless` and `/Terminal` | FNA3D, SDL_GPU | **Pre-existing**, the documented golden gap; unchanged from the previous pass |
| `CnjEffectTest.LoadsRealCnjFixture`, `CnjStockEffectTest.CustomGlslEffectStillWorks` | SDL_GPU | **Pre-existing**, the documented `Cnj*` GLSL-dialect gap |
| `RendererStrideConformance.EveryGltfStrideReachesTheNativeDrawBoundary` | SDL_GPU | **Pre-existing**, the stride-24 gap; see the corrected note in the previous pass's verification section for the evidence |

EasyGL went from 7392 passing (previous pass) to **7398 with zero product failures**, the difference
being this pass's own additions.

**Sanitizers.** `cmake-build-fna3d-asan` (`FNA3D`, `CNA_SANITIZE=address,undefined`, Debug) rebuilt
from these sources and run over the Effect/compiled-effect/XNB/sampler-cache subset: **929 tests
ran, 928 passed, 1 skipped, 0 failed**, and **zero AddressSanitizer findings**. Five distinct
UndefinedBehaviorSanitizer reports, every one of them inside **pinned MojoShader** and none in CNA
code -- the same five the previous pass recorded, at the same sites:

- `mojoshader_common.c:1050` -- `signed integer overflow: 1000000000 * 10 cannot be represented in
  type 'int'`, in its own string-to-number parsing;
- `mojoshader_effects.c:1617`, `:1641`, `:1825`, `:1831` -- `null pointer passed as argument 2,
  which is declared to never be null`, i.e. `memcpy`/`memset` with a null source and a zero length,
  which is technically undefined and practically harmless.

Nothing new and nothing attributable to this change set. LeakSanitizer is disabled for the run for
the reason the previous pass recorded (every record attributed to `libGLX_mesa.so`).

**Platform boundary gates.** `sdl_inventory --check`, `sdl_ratchet --check`, `hot_path_lint` and
`nonproduction_sdl_audit --check` all pass. `sdl_classify --check` and `renderer_sdl_audit --check`
fail on the same pre-existing items the previous pass recorded (the `SDL_WINDOWEVENT_*` symbols and
the `pixijs` family); neither is touched by this change set.

**Release build:** not run. The three trees above are Debug, which is what they were configured as;
this pass did not create a Release configuration and does not claim Release verification.

## 1. Executive conclusion

Compiled XNA effect support is CNA's largest remaining practical graphics-porting gap, but the
lowest-risk solution is smaller than the older project plans imply. CNA already contains an
FNA3D renderer, FNA3D already integrates MojoShader's Effect Framework runtime, and the repository
already carries genuine FNA/XNA `.fxb` binaries for the stock effects. The correct first milestone
is therefore a complete, production-quality implementation on FNA3D, followed by renderer waves
that reuse MojoShader's native backend adapters where those adapters exist.

The feature must **not** be implemented as another `CNAEXT::ShaderEffect` path. `ShaderEffect`
represents one caller-supplied vertex/fragment source pair. A compiled XNA effect is a stateful
container with reflected parameters, annotations, techniques, multiple passes, shader objects,
samplers, render-state assignments, default values, cloning semantics, and Content Pipeline
integration. Collapsing it into `ShaderEffect` would discard observable XNA behavior and would
make backend-specific shader text leak into the public graphics layer.

The proposed architecture adds a renderer-neutral compiled-effect runtime interface, keeps all
public XNA objects and mutable parameter values in the graphics module, and lets each renderer own
the native compiled shaders and pass application. FNA3D is the reference implementation and the
behavioral oracle. Unsupported renderers must reject construction explicitly; they must never
accept bytecode and silently draw with a stock shader.

## 2. Terminology and compatibility boundary

The phrase "compiled `.fx`" is commonly used but technically imprecise:

| Input | Meaning | Initial support |
|---|---|---:|
| `.fx` | HLSL Effect Framework **source** | Out of scope; CNA will not embed an HLSL source compiler |
| `.fxb` / XNB Effect payload | Compiled Direct3D 9 Effect Framework binary accepted by XNA/FNA `Effect(GraphicsDevice, byte[])` | In scope |
| `.mgfxo` / MGFX | MonoGame's separate `MGFX` container and profile format | Out of scope for v1; detect and reject precisely |
| CNAEXT `ShaderEffect` | Explicit renderer-specific GLSL/SPIR-V/source pair | Existing independent feature; unchanged |

The v1 contract is the legacy XNA/FNA Effect Framework bytecode used by FNA `.fxb` files and by
the canonical XNB `EffectReader`. Runtime source compilation and transparent ingestion of
MonoGame's MGFX format are separate projects. A format-sniffing error should tell users which
format they supplied instead of returning a generic shader compilation failure.

## 3. Audited CNA baseline

### 3.1 Public Effect model

- `modules/graphics/src/Xna/Effect.cpp` unconditionally throws `NotImplementedException` from the
  bytecode constructor.
- `Effect` is currently abstract because `Clone()` and `OnApply()` are pure virtual. FNA's base
  `Effect` is concrete: compiled effects use the base class, `OnApply()` defaults to no-op, and
  `Clone()` clones the native effect.
- The non-bytecode base constructor fabricates one `Default` technique containing one `P0` pass.
  This is useful for CNA's stock effects but is not valid reflection for a compiled effect.
- `EffectPass` stores only an owner and a technique identity. It does not store a native pass
  index, so every pass currently calls the same owner-level `Apply()` operation.
- `EffectParameter` stores convenient typed vectors and texture pointers, but it has no binding to
  compiled register storage, no dirty tracking, and no way to create nested array/structure views.
  Its current tightly packed array representation also does not model the float4 register padding
  used by legacy effects.
- `EffectAnnotation`, `EffectParameterCollection`, `EffectTechniqueCollection`, and related types
  provide most of the public API surface, but need internal population/build APIs and immutable
  reflected metadata.

### 3.2 Draw and renderer model

- `Effect::Apply()` calls `OnApply()` and then selects the effect on `GraphicsDevice`.
- 3D draw calls derive `GpuDrawParams` from the selected effect. `GpuDrawParams` currently carries
  a `customEffectRenderer` pointer for CNAEXT `ShaderEffect`, but no compiled-effect runtime token.
- `IEffectRenderer` and `IGraphicsRenderer::CreateEffectRenderer(vertexSource, fragmentSource)`
  model exactly one source pair. Extending this interface with techniques and passes would break
  its intentionally narrow contract.
- `GraphicsCapability::CustomEffects` currently describes the source-based `ShaderEffect` path.
  It cannot truthfully describe compiled effects: a renderer may support either feature without
  supporting the other.
- Effect pass state can be applied through the existing `GraphicsDevice` blend, depth/stencil,
  rasterizer, texture, and sampler state entry points. This avoids bypassing CNA's state caches.
- SpriteBatch has a separate custom-effect route and therefore needs an explicit compiled-effect
  integration test and draw path, not only 3D primitive coverage.

### 3.3 Content Pipeline model

- `KnownUnsupportedContentTypeReader` currently registers the canonical
  `Microsoft.Xna.Framework.Content.EffectReader` and throws an intentional unsupported error.
- CNA already has bounded/exact byte-reading helpers and stock effect readers returning
  `std::shared_ptr<Effect>`, which is the correct ownership shape for the general reader too.
- The real reader payload is simple: a signed 32-bit byte count followed by exactly that many
  bytes. The reader constructs `Effect`, assigns the asset name, and exposes native compilation
  failures as an asset-specific `ContentLoadException`.

### 3.4 Native dependency baseline

- `cmake/ThirdPartyFNA3D.cmake` pins FNA3D, and that checkout already pins and builds MojoShader.
- MojoShader exposes the complete Effect Framework API: effect compilation, reflection, cloning,
  technique selection, begin/pass/commit/end, and reported render/sampler state changes.
- The pinned MojoShader declares GLSL, Metal, HLSL, and SPIR-V profiles, including direct SPIR-V
  output and SPIR-V linking. The old proposal to add a mandatory GLSL-to-SPIR-V glslang hop is no
  longer correct. Actual enabled profiles still need compile-time/runtime probes because header
  declarations do not guarantee that every profile was built.
- FNA3D contains mature MojoShader adapters for OpenGL, D3D11, and SDL_GPU. These are valuable
  reference integrations and reduce backend risk substantially.
- `Fna3dStockEffect` already wraps `FNA3D_CreateEffect`, exposes the underlying reflected
  `MOJOSHADER_effect`, writes values, applies a pass, and owns native cleanup. This nearly proves
  the native half of the feature, but it is stock-effect-specific and hardcodes technique 0/pass 0.
- `Fna3dRenderer::PrepareDrawEXT` always applies a stock effect. A custom compiled pass would
  currently be overwritten immediately before drawing, so the draw dispatch must distinguish
  stock, source-based custom, and compiled effects.

### 3.5 Test assets already present

The six FNA3D stock binaries under `modules/renderers/fna3d/effects/` are genuine `.fxb` files with
documented upstream provenance. They remove the old plan's basic fixture-acquisition blocker and
are suitable for parser/reflection and real-shader pixel tests. The FNA3D tests additionally build
a deterministic Effect Framework 9.1 binary from the format consumed by pinned MojoShader. This
legally reproducible, state-only fixture covers two techniques, multiple passes, scalar/vector/
matrix/array defaults, annotations, exact pass identity, and every supported render-state token.
It deliberately does not impersonate compiler output. A compiler-produced fixture and independent
FNA oracle are still needed for structures, custom shader programs, textures, and samplers.

## 4. Corrections to the older FX plans

This plan supersedes the technical direction in `docs/fx-bytecode-support-plan.md`,
`docs/shader-effect-vs-fx-bytecode.md`, and Phase 74 of `plan_graphics.md`. Those documents remain
useful history but should be updated when implementation starts.

| Older assumption | Audited 2026 conclusion |
|---|---|
| MojoShader is absent from the tree | It is already pinned transitively by the FNA3D dependency |
| Test bytecode cannot be sourced | Six provenance-documented FNA `.fxb` files are already committed |
| Vulkan requires MojoShader GLSL plus glslang | Pinned MojoShader has a direct SPIR-V profile and linker |
| EasyGL should be the first implementation | FNA3D already has the native effect runtime and is the lowest-risk first backend |
| One generic custom-shader path is enough | Source-pair `ShaderEffect` and compiled XNA effects require distinct contracts/capabilities |
| All renderers should be attempted together | A gated FNA3D vertical slice should establish semantics before backend rollout |

## 5. Target architecture

```text
Effect(bytecode) / EffectReader
          |
          v
 public XNA object graph + mutable parameter storage
 (Effect -> Techniques -> Passes; Parameters -> Elements/Members)
          |
          | neutral descriptors, dirty values, technique/pass indices
          v
 ICompiledEffectRuntime  <---- created by IGraphicsRenderer
          |
          +-- FNA3D runtime: FNA3D/MojoShader effect (first implementation)
          +-- SDL_GPU runtime: MojoShader SDL adapter (later)
          +-- OpenGL runtime: MojoShader GL adapter (later)
          +-- D3D11 runtime: MojoShader D3D11 adapter (later)
          +-- other renderer adapters or explicit NotSupportedException
```

### 5.1 Renderer-neutral contracts

Add a distinct renderer-facing abstraction, provisionally named `ICompiledEffectRuntime`.
The final names may follow existing CNA conventions, but the ownership boundary must remain:

- `IGraphicsRenderer::CreateCompiledEffect(...)` accepts immutable bytes and returns an owned
  runtime or a structured failure.
- The common interface publishes neutral, immutable reflection descriptors. No `MOJOSHADER_*`,
  FNA3D, OpenGL, Vulkan, D3D, or Metal type may appear in public/common graphics headers.
- The runtime supports cloning, selecting a technique, applying an exact pass, synchronizing
  values/textures, returning neutral state changes, and exposing actionable diagnostics.
- `GraphicsCapability::CompiledEffects` is appended as a separate capability. Existing numeric
  capability values must not be reordered.
- `GpuDrawParams` receives a separate compiled-effect runtime/token. It is not aliased through
  `customEffectRenderer`.

An unsupported renderer returns no compiled runtime. The public constructor then throws a clear
`NotSupportedException` naming the active renderer and `CompiledEffects` capability. A malformed
or unsupported binary throws a distinct argument/graphics exception with the native parser
diagnostic. Callers must be able to distinguish platform capability from bad content.

### 5.2 Public object construction and parameter storage

Make the base `Effect` usable for compiled effects while preserving every stock-derived effect:

- provide a base `Clone()` implementation and a default no-op `OnApply()`;
- keep the existing stock-effect constructor behavior isolated from compiled reflection;
- populate collections from immutable runtime descriptors and select the first reflected
  technique;
- store exact technique and pass indices in the public wrappers;
- reject empty, oversized, malformed, MGFX, and unsupported-profile inputs predictably.

The public parameter graph should own a top-level backing value plus non-owning logical views for
array elements and structure members. Numeric storage must preserve MojoShader/FNA register
layout, including float4 padding, rather than treating every public array as tightly packed.
Descriptors must include name, semantic, class, type, row/column count, element count, byte/register
offset, annotations, default bytes, and sampler-to-texture relationships where applicable.

Mutable data belongs to each `Effect` instance:

- numeric/bool/int values live in bounded raw/register storage;
- strings and textures use typed side storage;
- setters validate compatible types and counts, update the correct subrange, and mark the owning
  top-level parameter dirty;
- getters expose XNA/FNA-compatible logical values rather than raw padded registers;
- only dirty top-level values are synchronized before a pass;
- a runtime sampler binds the texture referenced by its reflected texture parameter; uniform-name
  guessing is forbidden.

### 5.3 Pass application and state

`EffectPass::Apply()` is the semantic center of the feature. For a compiled effect it should:

1. validate that the pass belongs to the currently selected technique and effect;
2. call the overridable `OnApply()` hook;
3. synchronize dirty values and textures to the native effect;
4. select the exact native technique and pass;
5. apply/commit the native pass and obtain its render/sampler state changes;
6. translate all supported changes through `GraphicsDevice` state APIs;
7. select the compiled effect on `GraphicsDevice` for subsequent draws.

State changes should be grouped into copies of the current `BlendState`, `DepthStencilState`,
`RasterizerState`, and affected `SamplerState` objects, then installed atomically per group. The
mapping must cover the state set supported by FNA for compiled effects. Unknown state tokens must
raise a diagnostic exception in debug/tests and have an explicitly documented release policy;
silently ignoring a state is not acceptable.

The legacy CNAEXT owner-level `Effect::Apply()` behavior should remain compatible. For a compiled
base effect it should be defined and tested as applying pass 0 of `CurrentTechnique`, while direct
`EffectPass::Apply()` selects the requested pass. Stock effects must retain their existing path.

The active compiled runtime must survive until drawing. FNA3D's pre-draw path must skip
`ApplyStockEffectEXT` when it receives a compiled runtime, but must still bind vertex declarations,
streams, indices, textures, and other non-effect draw inputs. Arbitrary vertex semantics and
instancing must not be hardcoded to the stock-effect layouts.

### 5.4 Clone and lifetime semantics

`Clone()` must return an independent effect on the same `GraphicsDevice`:

- clone the native runtime when supported, otherwise recreate from retained immutable bytecode;
- rebuild all public collections so owner links point at the clone;
- copy current values, strings, and texture references;
- preserve `CurrentTechnique` by stable index;
- share only immutable compiled shader/cache data, never mutable parameters or current-pass state;
- permit either clone to be disposed independently.

Creation, application, reset, and destruction must respect each renderer's thread/context rules.
Native effect destruction must occur before its renderer/device disappears, and a disposed effect
must be removed safely if it is currently selected. Device disposal before effect disposal must
not dereference a dead backend.

### 5.5 Content integration

Enable the canonical `EffectReader` only after direct byte-array construction is stable:

- read a non-negative signed 32-bit length;
- enforce the existing ContentReader allocation limit before allocating;
- use exact reads so truncation cannot produce partially initialized bytecode;
- construct `std::shared_ptr<Effect>` using the ContentManager's device;
- assign the asset name;
- wrap parser/backend errors in an asset-specific `ContentLoadException` while retaining the
  actionable inner diagnostic;
- replace, rather than coexist with, the known-unsupported registration.

### 5.6 Dependency ownership

Do not build a second incompatible MojoShader copy. Before adding a common MojoShader target,
resolve dependency ownership explicitly:

- either expose the exact FNA3D-pinned MojoShader target to the FNA3D compiled-effect adapter; or
- factor `ThirdPartyMojoShader.cmake` into a single shared pin consumed by FNA3D and later native
  adapters.

The selected revision, configuration flags, enabled profiles, license, and notices must be visible
in CMake diagnostics and third-party documentation. A configure/build test must fail early when a
requested renderer lacks its required MojoShader profile or adapter.

## 6. Backend rollout strategy

| Wave | Renderer family | Proposed route | Initial disposition |
|---:|---|---|---|
| 1 | FNA3D | Generalize existing FNA3D/MojoShader effect ownership and state handling | Required MVP/reference backend |
| 2 | SDL_GPU | MojoShader SDL backend with its SPIR-V/Metal/native shader route | High priority after FNA3D |
| 2 | EasyGL/OpenGL family | MojoShader GL context and GLSL profiles | High priority; audit CNA/GL state-cache coherence |
| 2 | DirectX 11 | MojoShader D3D11 adapter | High priority on Windows |
| 3 | Vulkan | Direct MojoShader SPIR-V generation/linking and a CNA Vulkan pipeline adapter | Feasible; no mandatory glslang hop |
| 3 | Metal | MojoShader Metal profile plus CNA Metal pipeline adapter | Feasibility/prototype gate first |
| 3 | DirectX 9 | Native D3D9 bytecode/effect token route | Feasibility and platform test gate |
| 4 | D3D12 and middleware renderers | HLSL/SPIR-V/GLSL ingestion depending on renderer | Per-renderer feasibility gates |
| 4 | bgfx | Requires bgfx shader binary envelope/toolchain integration | Keep separate until a proven pipeline exists |
| N/A | fixed-function, 2D-only, and CPU renderers | No meaningful programmable shader target | Explicitly unsupported |

`CompiledEffects` becomes true only after a backend passes the shared constructor, reflection,
multi-pass, state, SpriteBatch, 3D, clone, lifecycle, and malformed-input contract tests. Producing
shader text or a native handle alone is not enough.

## 7. Security and robustness requirements

Compiled effects are untrusted binary input and MojoShader is a native parser. The feature must
include the following from its first mergeable implementation:

- strict maximum payload and reflected-object counts;
- checked arithmetic for byte offsets, register spans, dimensions, and array/member recursion;
- rejection of truncated strings, tokens, annotations, object tables, and cyclic/invalid views;
- no public object retaining pointers into a temporary parser buffer;
- exception-safe ownership during partial construction;
- fuzz seeds for every committed `.fxb` and a malformed/truncated corpus;
- ASan/UBSan runs over constructor, reflection, clone, apply, and destruction;
- deterministic diagnostics without dumping arbitrary binary contents or leaking addresses.

Shader caching is an optimization after correctness. A later cache may key immutable native shader
artifacts by bytecode hash, renderer/device/profile, and relevant build options. It must never share
mutable effect values, textures, selected techniques, or pass state between instances.

## 8. Implementation task list

The table is the complete work breakdown; the implementation snapshot above records which task
groups are delivered and which acceptance criteria remain open. Dependencies name the tasks that
must be accepted before a row can close.

### Phase A - Contract, oracle, and fixtures

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-001 | Freeze the v1 format boundary: XNA/FNA Effect Framework bytecode only; document MGFX and source `.fx` exclusions | - | Public docs and tests use unambiguous terminology |
| FX-002 | Record the FNA `Effect`, `EffectPass`, reflection, state translation, clone, and `EffectReader` behavior as the compatibility oracle | FX-001 | Reviewable mapping from every CNA behavior to FNA/XNA semantics |
| FX-003 | Inventory the six existing `.fxb` fixtures and preserve provenance, hashes, upstream revision, and license | FX-001 | CI verifies fixture hashes and provenance is auditable |
| FX-004 | Create a purpose-built conformance `.fx` source and committed `.fxb` covering techniques, passes, defaults, arrays, structures, annotations, textures, samplers, and states | FX-002 | **Done.** `modules/renderers/fna3d/effects/CnaConformanceEffect.fx` and its `.fxb`, compiled with `fxc` 9.29.952.3111 from the June 2010 DirectX SDK at profile `fx_2_0` -- the compiler XNA's own Content Pipeline used. Compiler identity, SDK hash, origin URL, output hash, reproduction commands and legal provenance are in that directory's README |
| FX-005 | Extend the FNA reference tool to emit normalized JSON reflection and deterministic pixels/state observations for all fixtures | FX-003, FX-004 | **Reflection and state halves done.** `tools/fna-reference --effects` runs FNA's own `Effect.INTERNAL_parseEffectStruct` over all seven committed binaries and writes `fna-effect-reflection.json`; `StockFixtureReflectionMatchesTheFnaOracle` compares CNA's reflection against it subtree by subtree. `--effect-states` goes further and builds a real managed FNA `GraphicsDevice` straight from an SDL window, applies every pass of every technique, and records what FNA's own `PipelineCache` installs on the public `BlendState`/`DepthStencilState`/`RasterizerState`/`SamplerStates` properties -- the same properties CNA writes in `Effect::ApplyCompiledPassState`, so they compare directly. `Fna3dEffectStateOracleTest.EveryPassInstallsTheStateFnaInstalls` replays every pass from the same starting device state the generator used and matches FNA on all seven binaries. Deterministic *pixel* observations from FNA remain the one piece not done; they are the least portable part of the oracle, since they depend on the GPU and driver rather than only on the translation |
| FX-006 | Add format sniffing tests for empty, random, truncated, valid FX bytecode, and MGFX | FX-001, FX-003 | Each category yields a stable, specific result |

### Phase B - Common compiled-effect architecture

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-010 | Append `GraphicsCapability::CompiledEffects` and add truthful defaults/matrix tests for every renderer | FX-001 | No existing enum value changes; all backends report false initially |
| FX-011 | Define renderer-neutral reflection descriptors, diagnostics, pass state-change types, and owned `ICompiledEffectRuntime` contract | FX-002 | Common headers contain no backend/MojoShader types and pass ownership is explicit |
| FX-012 | Add `IGraphicsRenderer::CreateCompiledEffect` with an unsupported default and device/thread ownership rules | FX-010, FX-011 | A mock backend can create/fail/destroy a runtime deterministically |
| FX-013 | Add a distinct compiled runtime/token to `GpuDrawParams` and draw dispatch without changing `ShaderEffect` | FX-011 | Stock, source custom, and compiled paths are mutually distinguishable |
| FX-014 | Make base `Effect` concrete with default `OnApply()` and compiled-aware `Clone()`, preserving every existing derived override | FX-011, FX-012 | All stock and CNAEXT effects compile and existing clone tests remain unchanged |
| FX-015 | Add private/internal collection builders and exact technique/pass identities | FX-011, FX-014 | Reflected order, name lookup, ownership checks, and exact pass selection unit tests pass |
| FX-016 | Implement bounded parameter backing storage, element/member views, defaults, strings, textures, and dirty revisions | FX-002, FX-011 | **Done, with one recorded divergence.** The FNA oracle now carries parameter *values*, and CNA matches it for every top-level scalar, vector, matrix and array in all seven committed binaries. Struct **members** deliberately differ -- see the note below |
| FX-017 | Populate annotations and sampler-to-texture bindings from descriptors | FX-016 | Annotation lookup/defaults and texture sampler associations match the oracle |
| FX-018 | Implement compiled constructor validation, reflection population, first-technique selection, and differentiated errors | FX-006, FX-012, FX-014, FX-015, FX-016, FX-017 | Valid fixtures construct; unsupported renderer and malformed/foreign formats fail distinctly |
| FX-019 | Implement independent compiled-effect clone and lifecycle/device guards | FX-018 | Values, textures, technique, owners, disposal, and device-loss tests pass without aliasing |

#### Recorded divergence: struct member values

The Effect Framework pads each row of a structure member out to a float4 register. CNA reads a
member through that padded stride; FNA reads it contiguously, so its members come back shifted.
Measured on `CnaConformanceEffect.fxb`, whose source declares
`Direction = float3(0.6f, 0.7f, 0.8f)` and `Thresholds = { 0.9f, 1.0f }`:

| Getter | CNA | FNA |
|---|---|---|
| `Lighting.Direction[0]` | `0.6` | `0` |
| `Lighting.Thresholds[0]` | `0` | `0.6` |

CNA returns what the source declares; FNA returns the parent's storage read at unpadded offsets.
This is the one place the compatibility oracle is not followed, and deliberately so -- matching FNA
here would mean returning a value the effect author never wrote. Top-level parameters agree
exactly, which is where real ports read their values, and the oracle comparison therefore asserts
values everywhere except inside a structure.

### Phase C - Pass and GraphicsDevice semantics

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-020 | Implement exact compiled `EffectPass::Apply` ordering and define compiled `Effect::Apply` as current-technique pass 0 | FX-015, FX-018 | Hook/order tests and multi-pass runtime mock tests match FNA |
| FX-021 | Synchronize only dirty parameters/textures before native pass application | FX-016, FX-020 | Mock counters prove correct first upload, no redundant upload, and subview invalidation |
| FX-022 | Implement complete neutral-to-CNA blend/depth/stencil/rasterizer state translation | FX-002, FX-020 | Table-driven tests cover every supported legacy render-state token and unknown-token policy |
| FX-023 | Implement texture/sampler state translation without mutating shared immutable state objects | FX-017, FX-020 | **Done.** Per-slot tests cover every filter-component combination, addressing, LOD bias, mip level, anisotropy, exact register targeting, reflected texture binding, and clone isolation |
| FX-026 | Carry `SamplerState.AddressW` through the renderer-neutral sampler contract | FX-023 | **Done.** `IGraphicsRenderer::ApplySamplerAddressW` carries the third axis, following the `ApplySamplerMipState` precedent: a separate default-no-op call each renderer adopts explicitly, rather than a signature change across 39 implementations. `GraphicsDevice` publishes it with the rest of a slot's sampler state, and FNA3D consumes it instead of mirroring U. A renderer that has not adopted it no longer invents a W of its own, so an effect's assigned `ADDRESSW` stands. Covered by `Fna3dSamplerAddressTests.cpp` through a new read-only `GetSamplerStateEXT`, which is where the assembled native state is observable. Renderers other than FNA3D adopt it as their volume-texture support warrants; EasyGL needs a profile guard because `GL_TEXTURE_WRAP_R` is ES 3.0+ |
| FX-024 | Integrate compiled selection/disposal with `GraphicsDevice` and every primitive draw entry point | FX-013, FX-019, FX-020 | Draws cannot use stale runtimes and stock application cannot overwrite compiled passes |
| FX-025 | Integrate compiled effects into SpriteBatch Begin/flush/end behavior | FX-024 | SpriteBatch pixel tests pass across multiple flushes and texture switches |

### Phase D - FNA3D vertical slice

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-030 | Resolve single-copy MojoShader/FNA3D dependency ownership, pinning, build flags, license, and profile probes | FX-011 | One compatible MojoShader instance is linked and configuration reports enabled profiles |
| FX-031 | Generalize/refactor `Fna3dStockEffect` internals into an owned arbitrary compiled-effect runtime | FX-030 | Creates and destroys each valid fixture with full native diagnostics |
| FX-032 | Translate `MOJOSHADER_effect` reflection into the neutral descriptor graph | FX-005, FX-016, FX-031 | CNA normalized reflection JSON equals the FNA oracle |
| FX-033 | Implement FNA3D value/texture upload, clone, technique selection, and exact pass lifecycle | FX-019, FX-021, FX-031, FX-032 | Mock/native tests cover defaults, mutation, clone, all techniques, and all passes |
| FX-034 | Translate FNA3D/MojoShader render and sampler state changes into the neutral state contract | FX-022, FX-023, FX-033 | State observations match FNA for the conformance effect |
| FX-035 | Prevent `PrepareDrawEXT` from overwriting a compiled pass while preserving vertex/index/stream binding | FX-024, FX-033 | Arbitrary semantics, indexed/non-indexed, multi-stream, and instancing tests render correctly |
| FX-036 | Enable `CompiledEffects` only for FNA3D and run shared capability/unsupported tests | FX-025, FX-032, FX-033, FX-034, FX-035 | FNA3D is true and every incomplete backend remains false with explicit rejection |
| FX-037 | Add FNA3D golden-pixel tests for 3D, SpriteBatch, multi-technique, multi-pass, parameters, textures, and pass states | FX-004, FX-036 | Deterministic results match the FNA oracle within documented tolerances |
| FX-038 | Add FNA3D lifecycle, reset, repeated-clone, and failure-injection tests | FX-019, FX-036 | **Done**, including the FX-052 sanitizer run over these cases. Device reset, four-generation clone chains disposed out of order, disposed-effect rejection, repeated mid-construction native failure, and repeated create/apply/dispose cycles all pass |

### Phase E - XNB Content Pipeline

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-040 | Implement the canonical general `EffectReader` with bounded exact payload reads and `shared_ptr<Effect>` result | FX-018, FX-036 | Direct and XNB-created effects have equivalent reflection and behavior |
| FX-041 | Replace the known-unsupported `EffectReader` registration and obsolete placeholder tests | FX-040 | Canonical reader resolves exactly once; unsupported backend error remains specific |
| FX-042 | Add valid, truncated, negative/oversized length, malformed bytecode, and MGFX XNB fixtures/tests | FX-006, FX-040 | No partial objects or unbounded allocation; errors include asset context |
| FX-043 | Add a game-level ContentManager sample/test that loads and renders a custom effect from XNB | FX-037, FX-041 | End-to-end asset load, parameter set, pass apply, and draw succeeds on FNA3D |

### Phase F - Hardening and FNA3D MVP gate

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-050 | Add parser/reflection limits and checked arithmetic throughout common and FNA3D paths | FX-032, FX-040 | Boundary tests prove all configured limits and overflow failures |
| FX-051 | Build a libFuzzer/AFL-compatible constructor/reflection/clone harness with the fixture corpus | FX-050 | **Done, and the gate is met at its documented bar.** Harness, seed corpus, deterministic in-build mutation corpus and a coverage-guided clang/libFuzzer campaign (`tools/graphics/compiled_effect_fuzzer.cpp`, `docs/fx-bytecode-fuzzing.md`). Forty-one crash classes found and fixed -- forty in pinned MojoShader via the managed patch, one in CNA's own `BuildSamplerMap` -- each kept as a named artifact in `tests/fixtures/compiled-effects/crash-corpus/` that the build replays. The bar was written down before it was met rather than after: one million coverage-guided executions per FNA3D driver, from the committed seed corpus, under ASan, with `SDL_ASSERT=abort`, producing no new artifact. Met on 2026-08-15 at commit `17bab8ee2` with margin -- 3,079,834 executions on the OpenGL/GLSL driver and 2,669,555 on SDL_GPU/SPIR-V, both full windows, zero findings. This bounds the exposure; it does not prove absence, and the porter guide still states the trust boundary |
| FX-052 | Run ASan/UBSan and renderer teardown/reset stress suites | FX-038, FX-050 | **Done for CNA-owned code, re-run 2026-08-17 over the repair pass with the same verdict.** 340 FX/Effect/XNB/capability tests pass under ASan+UBSan with zero address findings and zero CNA undefined-behaviour findings; the FX-038 reset and repeated create/apply/dispose stress cases run inside that suite. LeakSanitizer runs after all (the earlier ptrace claim was wrong) and attributes every leak record to pinned MojoShader's SPIR-V emitter or to `FNA3D_CreateDevice`, none to CNA. The remaining third-party UBSan/leak findings are recorded upstream findings, not a CNA gate |
| FX-053 | Benchmark construction, clone, dirty uploads, and draw overhead; add immutable artifact cache only if justified | FX-037 | **Done.** `tools/graphics/compiled_effect_benchmark.cpp` plus the baseline table in `docs/fx-compiled-effects.md`. Decision: **no cache**. Construction cost tracks embedded shader work rather than file size, `Clone()` is ~7.5x cheaper than constructing the same effect because the native clone reuses translated artifacts, dirty tracking keeps a no-change apply at ~2.9 us, and a compiled pass draws no slower than a stock effect. A bytecode-keyed cache would add cross-instance sharing risk for a case `Clone()` already covers |
| FX-054 | Run full stock-effect, `ShaderEffect`, SpriteBatch, model, primitive, and renderer regression suites | FX-037, FX-043, FX-052 | **Done.** The whole `CnaTests` binary runs under FNA3D: 5,997 pass and every remaining failure is explained -- one real regression from this branch (a stale FNA3D instancing message) fixed here, three `MouseCursorTest` failures caused by `SDL_VIDEODRIVER=offscreen` having no system cursors, one render-target readback that fails only on the SDL_GPU/Vulkan driver and passes on FNA3D's OpenGL driver, and one pre-existing FNA3D device-lifetime crash unrelated to compiled effects, now recorded in `known_bugs.md` |
| FX-055 | Publish FNA3D support documentation, format/error guide, capability matrix, dependency notices, and migration examples | FX-043, FX-054 | **Done.** `docs/fx-compiled-effects.md` covers the format boundary, loading, reflection, values, techniques/passes, published pass state, samplers, clone, lifetime, the renderer matrix, an error table, XNA-to-CNA migration, and the dependency/licence notices |
| FX-056 | Update/supersede the old FX plan documents and Phase 74 rows without erasing their historical record | FX-055 | **Done.** `docs/fx-bytecode-support-plan.md` and `docs/shader-effect-vs-fx-bytecode.md` carry supersession banners and point at the current guide; `plan_graphics.md` Phase 74 keeps its original rows and adds a row-by-row disposition (obsolete / delivered / re-scoped / carried forward) so none of them can be picked up again |
| FX-057 | Declare the FNA3D vertical slice usable | FX-051, FX-052, FX-054, FX-055, FX-056 | **Declared, 2026-08-15.** All eight exit criteria pass. The last two to close were the oracle criterion -- once `mono` and the June 2010 DirectX SDK's `fxc` became available, `FX-005` produced both a conformance source CNA controls and FNA's own reflection of every committed binary as checked-in test data -- and the fuzz gate, met at the bar recorded in `docs/fx-bytecode-fuzzing.md`. Assessed row by row in section 10.1. Scope of the claim: FNA3D only; section 10.2 governs the rest |

### Phase G - Additional renderer waves

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-060 | Extract a reusable shared backend conformance suite from the FNA3D tests | FX-057 | **Done, and materially widened by `FX-084`-`FX-088` (2026-08-17).** `tests/support/CNA/TestSupport/` holds the Direct3D 9 Effect Framework format constants, the deterministic fixture builders (a hand-assembled Shader Model 2.0 **pair** now -- pixel and vertex) and a contract section per shape: format, reflection, **parameter API**, techniques/passes, render state, state policy, samplers, texture binding, clone, lifecycle, and the drawing sections **enumerated in `RunCompiledEffectContract`'s own doc comment** (draw matrix, multi-stream, instancing, SpriteBatch and its multi-pass and texture-slot shapes, sampler pixels, pass selection, render-target source, stock-draw isolation, orientation, effect switching), plus an explicit unsupported-backend contract. That doc comment is the single place the drawing sections are listed; this row deliberately no longer carries a count, because three separate documents had drifted to three different ones (`FX-093`). A backend adds one test file that builds its device and calls them. FNA3D runs it and static-asserts the neutral constants against its parser's own enumerations. Every draw section reads a pixel back and compares it against the effect's own parameter, which is what makes the suite able to see a silent stock-shader fallback at all -- the nine original sections could not have |
| FX-061 | Implement and gate SDL_GPU through the MojoShader SDL adapter | FX-030, FX-060 | **Done -- `SupportsCompiledEffects()` reports true.** `cna_configure_mojoshader()` separates the dependency from FNA3D, the renderer-neutral translation moved to `modules/renderers/common/mojoshader` and is shared with FNA3D, and `SdlGpuCompiledEffect` creates, clones, reflects, selects techniques and passes, translates render and sampler states, and validates parameter and texture assignment against MojoShader's own SDL_GPU adapter. Two existence gates plus dedicated tests cover it, and one of those tests caught a real crash: several MojoShader parse failures are static sentinels rather than allocations, and deleting one walks static storage -- now guarded in the shared module for every backend. `FX-071` closed the remaining gaps: a real draw route, a golden-pixel test, and the FX-060 shared suite passing through the public `Effect`/`GraphicsDevice` API. Still refused explicitly rather than silently mishandled: vertex-stage sampling, 3D/cube textures, and multi-stream declarations |
| FX-062 | Implement and gate EasyGL/OpenGL-family support through MojoShader GL | FX-030, FX-060 | **Done -- `SupportsCompiledEffects()` reports true.** `EasyGLCompiledEffect` (`modules/renderers/easygl/{include,src}/.../EasyGLCompiledEffect.{hpp,cpp}`) creates, clones, reflects, selects techniques and passes, and translates render/sampler states against MojoShader's own OpenGL adapter, reusing the shared `CNA::Internal::Renderers::MojoShaderEffect` translation module FX-061 already built and the existence gate's calling-convention trampolines. Simpler than the SDL_GPU runtime in two ways the existence gate predicted: no separate link step (`MOJOSHADER_glBindShaders` links and binds in one call) and no uniform-snapshot capture (EasyGL draws immediately, no `Present()`-deferred queue). New `CNA_EASYGL_COMPILED_EFFECTS` CMake option, off by default. `EasyGLRenderer::BindCompiledEffectForDrawEXT` closed the remaining gap (`FX-062`'s own equivalent of `FX-071`): it matches the caller's `VertexDeclaration` against the applied pass's reflected vertex attributes (`MOJOSHADER_glSetVertexAttribute` per match), binds each reflected pixel-stage sampler's texture, and calls `MOJOSHADER_glProgramReady()` then `MOJOSHADER_glProgramViewportInfo()` before a new compiled-effect branch in `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatches the draw -- ahead of the stock declaration guard, mirroring the SDL_GPU draw route. Verified by 13 tests, including a golden-pixel render-target test (byte-identical expected RGBA to SDL_GPU's, cross-validating the shared MojoShader preshader fix a third time) and the FX-060 shared suite, plus a full 6910-test `CnaTests` regression run on `cmake-build-easygl` (OPENGLES3) with zero new regressions (5 failures, all pre-existing/environmental). Also fixed a latent bug in `GraphicsDeviceCapabilityTests.cpp`'s `kExpectCompiledEffects`: it checked renderer selection alone, which would have wrongly expected `true` for a plain SDL_GPU/EasyGL build lacking their off-by-default `CNA_SDL_GPU_COMPILED_EFFECTS`/`CNA_EASYGL_COMPILED_EFFECTS` opt-in. **Superseded in part by the 2026-08-17 repair pass:** the four gaps this row originally recorded as "refused explicitly" for SpriteBatch, sampler state, instancing and multi-stream were not all refusals -- SpriteBatch and instancing silently used the stock shader instead. All four are now implemented (`FX-080`, `FX-082`, `FX-083`). What remains genuinely refused by name: vertex-stage sampling and 3D/cube sampler bindings. Accepted-and-inert, documented rather than approximated: `AddressW`, and `MipMapLevelOfDetailBias` on the OpenGL ES profiles |
| FX-063 | Implement and gate DirectX 11 through the MojoShader D3D11 adapter | FX-030, FX-060 | **Not implemented; requires an executable Windows environment.** *Platform:* Windows with a real D3D11 device. *Dependencies:* the pin ships `mojoshader_d3d11.c`, so unlike Vulkan this backend gets a ready-made adapter -- but it compiles HLSL at runtime through `D3DCompile`, loaded from `d3dcompiler_47.dll` (`LOAD_D3DCOMPILER` in that file; it also has a `dlsym` path, so a Wine/vkd3d configuration is worth trying before assuming Windows-only). MojoShader's `SUPPORT_PROFILE_HLSL` must be ON, which the FNA3D pin's CMake leaves off unless `PROFILE_HLSL` is set. *Integration point:* a `D3D11CompiledEffect` implementing `ICompiledEffectRuntime`, created from `D3D11Renderer::CreateCompiledEffect`, with the nine-function `MOJOSHADER_effectShaderContext` taken from the adapter rather than hand-written (contrast Vulkan, `FX-065`). *Risk:* the adapter's context-pointer convention differs per adapter -- the OpenGL one omits the leading context argument entirely (`FX-062` finding), so verify the D3D11 typedefs against the header before casting anything into those slots. **Requirements for whoever picks this up (written 2026-08-18, on a machine that cannot run it).** Before the capability may be flipped, ALL of these shared contracts must pass through the public `Effect`/`GraphicsDevice` API, not through the runtime directly: the backend conformance contract, the draw matrix (buffered/user, indexed/non-indexed, non-zero `baseVertex`/`startIndex`), pass selection, effect switching, sampler pixel state, render-target sampling, stock-draw isolation, orientation, SpriteBatch (slot 0, multi-pass ordering, texture slot), cube/volume samplers, the many-draws uniform contract, and the truncation contract. Multi-stream and instancing must either pass or be skipped on the backend's OWN named refusal -- silence there is the FX-080 defect. Anything the backend genuinely cannot do must throw a named `NotSupportedException`, never fall through to a stock shader. |
| FX-064 | Prototype direct MojoShader SPIR-V generation/linking for Vulkan | FX-030, FX-060 | **Done -- existence gate proven, real Vulkan device, no glslang.** `tools/graphics/mojoshader_vulkan_probe.cpp` implements the nine-function `MOJOSHADER_effectShaderContext` backend directly against `MOJOSHADER_parse(MOJOSHADER_PROFILE_SPIRV, ...)`, since MojoShader ships no Vulkan adapter (there is no `mojoshader_vulkan.c`, unlike GL/SDL_GPU/D3D11), then builds descriptor set layouts, a pipeline layout, shader modules and a graphics pipeline from raw Vulkan calls -- the probe IS the prototype adapter this task exists to produce. Renders offscreen through `VK_KHR_dynamic_rendering`, no swapchain, no SDL, no CNA. All three technique/pass combinations of `CnaConformanceEffect.fx` render correctly on the first attempt against a real Intel iGPU with the Khronos validation layer enabled throughout (zero warnings/errors): `MainPixelShader` lands on `(3,6,10,13)` and `FlatPixelShader` on `(20,41,61,82)` -- byte-identical to the SDL_GPU and OpenGL backends' own established golden pixels, a third independent cross-confirmation of the shared preshader register-count fix (FX-051/FX-071 lineage), never re-triggered here. See the existence-gate findings below the task table for the real findings this surfaced (the "spirv"/"glspirv" profile-string split, why no shader-side viewport flip is needed for Vulkan, and the public API's only route to the trailing SpirvPatchTable's size) |
| FX-065 | Complete and gate Vulkan after the prototype | FX-064 | **Done (2026-08-18): runtime and draw route both, capability TRUE.** `VulkanCompiledEffect` turns FX-064's probe into a real runtime: `modules/renderers/vulkan/{include,src}/.../VulkanCompiledEffect.{hpp,cpp}`, behind the new `CNA_VULKAN_COMPILED_EFFECTS` option (off by default, shaped exactly like its EasyGL and SDL_GPU siblings). This is the first backend with **no MojoShader-provided adapter** -- there is no `mojoshader_vulkan.c` -- so the nine-function `MOJOSHADER_effectShaderContext` is CNA's own, written directly against `MOJOSHADER_parse()` with the portable SPIR-V profile, and everything the other two inherit from their adapter (shader ref-counting, the bound vertex/pixel pair, the constant register files behind `mapUniformBufferMemory`, the uniform packing) is code in this repository. `LinkAndGetShadersEXT` performs the separate `MOJOSHADER_linkSPIRVShaders` step -- which patches the vertex shader's input types to the real vertex format, links vertex outputs to pixel inputs, and returns the patch-table size that must be subtracted from `output_len` before the SPIR-V reaches `vkCreateShaderModule` -- and the modules are (re)created per link, because linking patches the SPIR-V in place and a cached module would carry a previous draw's vertex layout. `CaptureUniformSnapshotEXT` packs the applied pass's registers at apply time, since this renderer records at `Present()` and the register files are shared by every effect it owns. Verified by 8 tests going through `CreateCompiledEffect` directly (`VulkanCompiledEffectTests.cpp`): the compiler-produced conformance fixture reflects identically, **every one of the six committed XNA stock effects compiles to SPIR-V**, a pass publishes its render states and its `sampler_state` block (`AddressU = Mirror`, `AddressV = Clamp`, `MaxAnisotropy = 8`, straight from the `.fx` source), a repeated apply of one pass re-publishes it (FX-101's defect, checked here from the start), technique/pass selection is bounded, clone independence and either-order disposal hold, malformed input is refused, and eight create/apply/clone/dispose cycles stay stable. **The draw route landed the same day, and the capability is now TRUE.** `EnsureCompiledEffectResourcesEXT` builds the four descriptor set layouts MojoShader's SPIR-V profile fixes (`VS_SAMPLER`/`VS_UNIFORM`/`PS_SAMPLER`/`PS_UNIFORM` at sets 0..3; set 0 is always the empty layout because vertex-stage sampling is refused by name, `FX-109`) plus a per-frame uniform ring for each stage, addressed by dynamic offset; `GetOrCreateCompiledEffectPipelineLayoutEXT` keys the pixel-sampler set layout on the exact set of sampler registers the shader declares; `GetOrCreateCompiledEffectPipelineEXT` builds the pipeline from the linked SPIR-V pair and the pass's own attribute descriptions; and `PrepareCompiledEffectDrawEXT` captures everything the `Present()`-deferred replay needs at the moment the draw is issued, because the register files are shared by every effect this renderer owns. `SpriteBatch` gets its own route (`QueueCompiledEffectSpriteEXT`), which leaves the stock sprite pipeline entirely and applies the technique's passes at XNA's own flush granularity -- run-major over a contiguous same-texture run, then pass-major within it. **19 of the 22 shared contract sections pass on a real device (llvmpipe, Vulkan 1.1); the other three refuse by name and skip**: multi-stream vertex input, instancing, and a `Texture3D` bound to a pixel sampler. Five defects were found and fixed while getting there, each of which would have produced wrong pixels rather than an error: the shader module was owned by the effect and destroyed at the next link, so a deferred draw recorded a dangling handle (modules are now renderer-owned and content-addressed, which `vkCreateShaderModule`'s own copy makes correct); the pipeline cache keyed on the shader pair alone, so two passes sharing a module bound each other's vertex attribute layouts; `MakeExt3DKey` buckets the vertex stride rather than carrying it, which is right for the stock routes and wrong for a compiled one whose vertex input comes from its own declaration; the sampler cache key carried only filter/address/anisotropy, dropping `MaxMipLevel`, the LOD bias and `AddressW` entirely (`FX-091`'s defect, in this renderer's own cache); and `ApplySamplerState` left the mip clamp of whoever wrote the slot last in place, so a stock sprite drawn after a compiled effect inherited the effect's `MaxMipLevel` (`FX-092`'s defect). Vulkan's clip space has Y pointing down where D3D9's points up, and every stock shader here compensates with its own `pos.y = -pos.y`; a MojoShader-translated shader carries no such line, so the flip moves to a negative-height viewport for compiled draws. **Closed on the same day by `FX-112`'s closure pass**, which took it from 19/22 shared sections to 24 passed / 1 skipped: compiled instancing and pixel-stage volume sampling both work now, and the single remaining skip -- multi-stream vertex input -- is renderer-wide rather than an FX gap. One finding recorded rather than fixed here: a truncated effect binary trips an assertion inside pinned MojoShader's own effect parser (`mojoshader_effects.c` `readvalue`), and MojoShader's `assert` resolves to SDL's, whose default handler BLOCKS for an interactive answer -- so such an input hangs a whole suite run instead of failing it. The Vulkan backend itself rejects that input correctly (measured: the case passes in 70 ms with SDL's assertion hint set to always-ignore). CNA's test suite should install a non-interactive assertion policy; that is a harness task, filed as `FX-111` and **done the same day** -- the case is now in this backend's own malformed-input test, sweeping every truncation of the real fixture |
| FX-112 | Linux-verifiable FX closure pass: finish Vulkan, fix the shared suite's own gating | FX-065, FX-110 | **Done (2026-08-18).** Five separate findings, each verified by execution rather than by reading. **(1) A silent fallback in Vulkan's instanced route.** `DrawInstancedPrimitivesEx` never looked at `params.compiledEffectRuntime`, so a compiled Effect handed to `GraphicsDevice.DrawInstancedPrimitives` was drawn with a stock instanced shader -- the exact FX-080 defect, in a route that pass had not covered. **(2) The shared suite's instancing contract was gated on the wrong capability.** It required `Instancing && MultiStreamVertexInput`, but its own shape binds one PER-VERTEX stream and one PER-INSTANCE stream, which `GraphicsDevice::SetVertexBuffers` lets through without consulting `MultiStreamVertexInput` at all. Every renderer without multi-stream input was therefore excused from a contract it could actually run, and a skip reads as coverage. The gate is now `Instancing` alone; the one section that genuinely binds two per-vertex streams (the divisor-leak check) keeps its own `MultiStreamVertexInput` guard, and a backend that cannot draw the shape now skips on its OWN named refusal -- which is how SDL_GPU's skip is now recorded, instead of on a capability that did not describe it. **(3) Compiled instancing on Vulkan, which turned out to be reachable.** With the gate corrected the contract runs here, so it was implemented rather than refused: `LinkAndGetShadersEXT` now takes a stream list and emits one `VkVertexInputBindingDescription` per stream with the right input rate, attributes carry their stream's binding index, and the pipeline key mixes both. The per-instance route's existing frequency expansion (REMED-GFX-213) means binding 1 keeps an implicit divisor of 1 and no frequency reaches the pipeline. Both halves of the contract pass, including the one with non-zero `baseVertex`, `startIndex` and per-instance `VertexOffset`. **(4) Pixel-stage volume sampling on Vulkan** -- see `FX-110`. **(5) Two shared contracts promoted from one backend to all four**: `RunCompiledEffectManyDrawsContract` (600 draws in one frame, each keeping its own uniform values -- the shape that catches a constant ring that WRAPS, which every backend has some version of) and `RunCompiledEffectTruncationContract` (every 4-byte truncation of the committed fixture refuses or parses whole). Measured while writing the second: through the public `Effect` constructor no truncation reaches MojoShader's `readvalue` assertion, because CNA's own container validation refuses first -- so that contract is about the public boundary's robustness and the proof of `FX-111`'s policy stays the sweep that goes at a renderer's runtime directly. Result across the four backends: FNA3D 47 passed / 0 skipped, SDL_GPU 33 / 3, EasyGL 27 / 1, Vulkan 24 / 1, every skip a named refusal or a renderer-wide capability. Mutation-tested: the sampler mip clamp, uniform-chunk selection, per-pass pipeline identity, the render-target Y flip and the per-instance input rate each fail their own tests when broken |
| FX-111 | Install a non-interactive assertion policy in the test suite | FX-065 | **Done (2026-08-18).** `tests/HarnessAssertionPolicy.cpp` selects `always_ignore` through SDL's own `SDL_ASSERT` environment hint from a static initialiser, with `overwrite=0` so an explicit `SDL_ASSERT=abort` in the environment still wins when a stack is wanted. Deliberately the hint and not `SDL_SetAssertionHandler`: this translation unit is compiled into `CnaTests` for every platform and renderer combination, including the ones that link no SDL at all, and calling the native function would make the whole suite depend on a library most configurations have no other reason to link. Nothing in the file includes an SDL header or names an SDL symbol. `always_ignore` over `abort` is a real trade-off rather than an obvious choice, and the file says so: `abort` gives a clean stack but kills the whole run for one bad input, only marginally better than the hang; `always_ignore` returns and lets the caller's own error handling decide, at the cost of continuing past an invariant the third-party library considered violated. **Verified by making the case that used to hang into the test that proves the policy**: `MalformedBytecodeIsRejectedWithoutCrashing` now sweeps every 4-byte truncation of the real 5000-byte `CnaConformanceEffect.fxb` (1249 of them, 95 ms) and requires each to be either refused or parsed WHOLE -- never a half-built runtime, and never a wedge. Truncation at 1544 bytes reaches `mojoshader_effects.c:430`'s `readvalue` assertion; with the policy neutralised (`SDL_ASSERT=not_a_real_policy`, which falls back to the default handler) that same run terminates on SIGABRT with stdin at /dev/null, and blocks for an interactive answer on a real terminal, which is the originally reported hang. One thing worth recording rather than assuming: `sdl_inventory.py` scans `modules/` only, so a top-level `tests/` file is outside the SDL gates' reach entirely -- this one needs no budget entry, and would classify cleanly if the scan roots were ever widened |
| FX-066 | Prototype and gate Metal support if the pinned profile meets CNA requirements | FX-030, FX-060 | **Not implemented; requires macOS.** *Platform:* macOS (or iOS) with a real Metal device. *Dependencies:* MojoShader's `SUPPORT_PROFILE_METAL` must be ON -- the pin's CMake leaves it off unless `PROFILE_METAL` is set, and enabling it also pulls `-lobjc`. There is **no** `mojoshader_metal.c` binding glue in the pin, so like Vulkan this backend needs CNA to write the nine-function context itself, against Metal Shading Language source output rather than a binary IR. *Integration point:* a `MetalCompiledEffect` implementing `ICompiledEffectRuntime`; note that `docs/metal-shader-effect-contract.md` already describes this renderer's own shader-source contract and should be read first. *Risk:* the Metal profile emits MSL text that must be compiled through `MTLDevice.newLibraryWithSource`, which is a per-pass runtime compile -- caching strategy is a design decision this task owns, not an afterthought. **Requirements for whoever picks this up (written 2026-08-18, on a machine that cannot run it).** Before the capability may be flipped, ALL of these shared contracts must pass through the public `Effect`/`GraphicsDevice` API, not through the runtime directly: the backend conformance contract, the draw matrix (buffered/user, indexed/non-indexed, non-zero `baseVertex`/`startIndex`), pass selection, effect switching, sampler pixel state, render-target sampling, stock-draw isolation, orientation, SpriteBatch (slot 0, multi-pass ordering, texture slot), cube/volume samplers, the many-draws uniform contract, and the truncation contract. Multi-stream and instancing must either pass or be skipped on the backend's OWN named refusal -- silence there is the FX-080 defect. Anything the backend genuinely cannot do must throw a named `NotSupportedException`, never fall through to a stock shader. |
| FX-067 | Assess DirectX 9, D3D12, LLGL, Diligent, Magnum, Sokol, Wicked, and other programmable renderers individually | FX-060 | **Done.** Section 10.3 classifies every one of the 46 renderer identities as planned, assessed-feasible or unsupported-by-design, from two measured facts: which profiles the pinned MojoShader actually compiles (GLSL in four dialects and SPIR-V portably; HLSL Windows-gated, Metal Apple-gated; ARB1/BYTECODE/D3D disabled) and which APIs it ships binding glue for (OpenGL, SDL_GPU, D3D11 -- and nothing else). The finding worth acting on is DirectX 9: a compiled XNA effect *is* D3D9 bytecode, and CNA's D3D9 renderer already feeds raw DWORD token blobs to `CreateVertexShader`/`CreatePixelShader`, so it needs the container parsed and no shader translated at all -- filed as `FX-070` |
| FX-068 | Keep bgfx false until a reproducible bgfx-native shader packaging route is proven | FX-060 | Feasibility record covers shaderc format, reflection, pass states, and redistribution |
| FX-069 | Publish the final cross-renderer support matrix and project-wide completion definition | FX-061, FX-062, FX-063, FX-065, FX-066, FX-067, FX-068 | **Blocked, and only on platform access.** Four of its five renderer dependencies are done and verified on real devices (`FX-061` SDL_GPU, `FX-062` EasyGL, `FX-065` Vulkan, plus FNA3D from `FX-057`). It cannot close until `FX-063` (DX11), `FX-070` (DX9) and `FX-066` (Metal) have been executed somewhere they can run. The interim matrix in section 10.2 is accurate as of 2026-08-18 and says which rows are measured and which are unwritten |
| FX-070 | Implement and gate DirectX 9, which needs the effect container parsed but no shader translated | FX-060, FX-067 | **Not implemented; requires an executable Windows or DXVK-native environment.** *Platform:* Windows with a real D3D9 device, or DXVK-native on Linux -- the latter is the one of the three remaining backends with a plausible Linux path, and establishing whether it actually works is the first task rather than an assumption. *Dependencies:* no shader translation at all. `MOJOSHADER_compileShaderFunc` receives the raw D3D9 token buffer, so the backend's compile step is `CreateVertexShader`/`CreatePixelShader` on that buffer. Reflection still comes from `MOJOSHADER_parse`, whose non-translating `BYTECODE` profile the pin DISABLES (`SUPPORT_PROFILE_BYTECODE=0`), so this task either re-enables it for its configuration or parses through a translating profile and discards the output. *Integration point:* a `Direct3D9CompiledEffect` implementing `ICompiledEffectRuntime`; the nine-function context is hand-written as for Vulkan, but the compile function is nearly trivial. *Risk:* the two facts the `cna_mojoshader_effect_probe` existence gate established -- the parser refuses to run without a nine-function backend context, and reflection comes from `MOJOSHADER_parse` -- are the whole design constraint; confirm both still hold against the pin in use before starting. Structurally the smallest backend in the project. Structurally the smallest backend in the project: `MOJOSHADER_compileShaderFunc` receives the raw D3D9 token buffer, so the backend's compile step is `CreateVertexShader` / `CreatePixelShader` on that buffer with nothing translated. Its exit criteria are the same shared-contract list written out on `FX-063`. |
| FX-071 | Give the SDL_GPU renderer a compiled-effect draw route | FX-061 | **Done -- `SupportsCompiledEffects()` reports true.** `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch a compiled-effect draw to a new `DrawKind::CompiledEffect` deferred command (`QueueCompiledEffectDraw`/`GetOrCreatePipelineCompiledEffect`/`IssueCompiledEffectDraw`, `SdlGpuRenderer.cpp`), mirroring the eight stock families' own queue/upload/replay pattern. `SdlGpuCompiledEffect` gained the three pieces the draw route needs: `BuildCompiledEffectVertexAttributes`/`BuildMojoShaderVertexAttributes` (a generic `VertexDeclaration`-to-`SDL_GPUVertexAttribute` builder, `SdlGpuCompiledEffectVertexLayout.hpp`/`.cpp`, matched against the applied pass's own vertex shader reflection), `LinkAndGetShadersEXT` (the separate, explicit `MOJOSHADER_sdlLinkProgram` step effect-framework binding never performs on its own, producing `SDL_GPUShader` handles MojoShader's linker cache keeps valid for this renderer's whole context lifetime), and `CaptureUniformSnapshotEXT` (packs `MOJOSHADER_sdlMapUniformBufferMemory`'s register files into each shader's uniform-buffer bytes immediately after `ApplyPass`, since this renderer defers GPU submission to `Present()` and the shared register files may be overwritten by then). The pipeline cache (`GetOrCreatePipelineCompiledEffect`) is keyed on shader identity and vertex layout rather than a fixed shader field. A real upstream MojoShader quirk surfaced and was worked around: `MOJOSHADER_sdlCompileShader` always reports at least one sampler slot per shader stage (an off-by-one against zero reflected samplers), so SDL_GPU's own binding-count validation requires a dummy binding (this renderer's default white texture) for every unreflected slot, not just the reflected ones. SpriteBatch draws now use a compiled effect too: `Effect::GetCompiledRuntimePtr()` lets `SdlGpuSpriteBatchRenderer::Draw` recognize one the same way it already recognized a ShaderEffect via `GetEffectRendererPtr()`, and `QueueSprite`/`IssueSpriteDraw` gained a third branch alongside the stock and custom-ShaderEffect ones, built against `SpriteVertex`'s own fixed layout (verified against FNA's `SpriteBatch.cs`: no `MatrixTransform` auto-set for a custom effect, and `Textures[0]` is unconditionally overwritten with the drawn texture after the effect's pass applies, which the existing trailing sampler bind already replicated). The ordinary-draw and SpriteBatch routes share one implementation (`BuildCompiledEffectBindingEXT`/`BindCompiledEffectForDrawEXT`, `CompiledEffectBinding`) and one pipeline cache rather than two that could drift apart. The golden-pixel blocker (see the investigation note below) turned out to be two register-count/float-count unit bugs in CNA's own MojoShader preshader-robustness patch, not a renderer bug -- fixed there, then closed out here with a real golden-pixel test (`RendersTheAppliedPassesExpectedPixelsIntoARenderTarget`, through `RenderTarget2D::GetData()`) and the FX-060 shared conformance suite (`SharedBackendConformanceContract`) now passing through the public `Effect`/`GraphicsDevice` API. `SupportsCompiledEffects()` flipped to `true`, and `GraphicsDeviceCapabilityTest.SupportsCompiledEffectsOnlyOnCompletedBackends` updated to expect it. **Amended by the 2026-08-17 repair pass:** the SpriteBatch route worked only when the caller had applied a pass by hand -- `SpriteBatch.Begin(..., effect)` applied none -- and the sampler binding dropped `MaxMipLevel`/`MipMapLevelOfDetailBias` and overwrote an unassigned slot with a default `SamplerState` instead of the game's own. Both fixed (`FX-080`, `FX-083`). Verified by 22 passing tests (`SdlGpuCompiledEffectTests.cpp`) and a full 6828-test `CnaTests` regression run (6795 passed; the 6 failures are pre-existing/environmental -- the documented `CnjEffectTest`/`CnjStockEffectTest` GLSL-dialect gap, an `SDL_VIDEODRIVER=offscreen` window-operations limitation, an order-dependent `PollEvents` flake, and the documented `GameEventSemanticsGoldenTest`/Headless/Terminal gap now also confirmed on SDL_GPU -- none touched by this task). Still open, each refused explicitly rather than silently mishandled: a compiled effect's vertex shader sampling a texture, a 3D/cube (not 2D) sampler binding, and more than one vertex stream |

#### FX-071 golden-pixel investigation (2026-08-16, closed)

The FX-060 shared conformance suite passes (see below for how that was verified) -- reflection,
state translation, and lifecycle were never the problem. A golden-pixel attempt initially produced
zero non-background pixels with every GPU-call input verified correct (vertex buffer, linked
shaders, attribute layout, uniform buffer sizes, a byte-verified identity `Transform`, a valid
sampler texture) and no SDL_GPU validation complaint, which first looked like it would need a
visual/RenderDoc-class frame debugger to diagnose further.

It didn't. A standalone existence-gate spike, `tools/graphics/mojoshader_sdlgpu_probe --render
<file.fxb>`, reproduced the identical all-black result with **zero CNA code** -- MojoShader and
SDL_GPU only, no FNA3D, no renderer, no `Effect`/`GraphicsDevice`. That isolated the defect
upstream of CNA's integration entirely, and let the rest of the investigation proceed by
instrumenting MojoShader directly (a scratch build, never committed) instead of guessing through
CNA's own draw path blind.

The root cause: **two register-count/float-count unit mismatches in CNA's own
`mojoshader-6333f74-effect-parser-robustness.patch`** (the FX-051-era security-hardening patch that
added bounds checks to MojoShader's Effect Framework preshader interpreter), not a CNA renderer bug
and not an upstream MojoShader defect.

- `run_preshader()`'s `inregs_count` bound was set to `preshader->register_count` -- a
  **vec4-register** count -- and then compared directly against `index`, which is a **float**
  offset into that same array (matching `copy_parameter_data`'s own `wanted = register_count << 2`
  convention, and matching how `outregs_count` is already correctly pre-multiplied by its caller).
  Any preshader input spanning more than the first register -- which is any struct or array wider
  than one float4, e.g. `CnaConformanceEffect.fx`'s `Lighting` struct or `Weights[2]` array -- read
  an `index` past the falsely-small bound and hit the new `return`, silently aborting the *entire*
  preshader mid-evaluation and leaving its output register at whatever it was before (zero).
- The same unit mismatch existed at the call site that fills a preshader's own input buffer:
  `copy_parameter_data(..., pd->preshader->registers, NULL, NULL, pd->preshader->register_count, 0,
  0)` passed the destination bound in register units too, truncating the copy itself before
  `run_preshader` even ran.

Both were one-line fixes (`<< 2` on each register-count value, matching the `wanted`/`outregs_count`
convention already used everywhere else in the same file), verified with the standalone probe
against three technique/pass combinations exercising both bugs independently (`CnaConformanceEffect`
technique 0 pass 0 -- `MainPixelShader`, a struct-driven preshader computing `saturate(...)`;
technique 0 pass 1 and technique 1 pass 0 -- `FlatPixelShader`, an array-driven preshader computing
`Tint * Weights[1]`): all three now render deterministic, non-black, independently
hand-verified-correct pixel values (e.g. `FlatPixelShader` renders `(20,41,61,82)`, an exact byte
match for `0.8 * (0.1,0.2,0.3,0.4)` in sRGB-free UNORM8). The fix was regenerated as a clean
`git diff` from the pristine 6333f74 checkout (not hand-edited into the existing unified diff) and
re-verified to `git apply --check` cleanly against a fresh pristine checkout before landing.

An earlier pass of this same investigation chased a different, incorrect lead: a `0xDEADBEEF`
sentinel `OpDecorate ... Location` found by dumping the vertex/pixel SPIR-V and disassembling with
`spirv-dis`. That was a red herring caused by dumping the SPIR-V *before*
`MOJOSHADER_sdlLinkProgram` ran -- the sentinel is patched to a real interface location by
`MOJOSHADER_linkSPIRVShaders`/`MOJOSHADER_spirv_link_attributes` as part of linking, and the
post-link SPIR-V (dumped and disassembled the same way) shows correct, matching `Location`
decorations on both sides of the vertex-output/pixel-input interface. No SPIR-V code generation bug
exists; this can be treated as closed.

**Closed.** `SdlGpuCompiledEffectDrawTest.RendersTheAppliedPassesExpectedPixelsIntoARenderTarget`
draws `CnaConformanceEffect.fxb`'s P0 (`MainPixelShader`, texture-sampling, struct-driven preshader)
and P1 (`FlatPixelShader`, no sampling, array-driven preshader) into a `RenderTarget2D` through the
real `SdlGpuRenderer::DrawPrimitivesEx` path and reads the centre pixel back via
`RenderTarget2D::GetData()`, matching the standalone probe's independently hand-verified values
exactly. `SdlGpuCompiledEffectTest.SharedBackendConformanceContract` runs the FX-060 shared suite
through the real public `Effect`/`GraphicsDevice` API. `SdlGpuRenderer::SupportsCompiledEffects()`
now returns `true`.

Writing the golden-pixel test surfaced one more non-obvious gotcha, worth recording for the next
renderer that adds one: `CompiledEffectDeviceState`'s `blend`/`depthStencil`/`rasterizer` fields are
only consulted by `ApplyPass()` for a state GROUP the applied *pass itself* assigns (a legacy D3D9
Effect Framework pass rebuilds a whole state group from the device's current state only when it
touches that group at all) -- `CnaConformanceEffect.fx`'s P0/P1 assign no render states at all, so
passing a `CompiledEffectDeviceState::rasterizer` override into `ApplyPass()` for them is silently a
no-op. What actually matters at draw time is `SdlGpuRenderer::CaptureRenderState()`, which reads the
renderer's own live cull/blend/depth state -- set via the ordinary public
`GraphicsDevice.RasterizerState`/`BlendState`/`DepthStencilState` properties, exactly like any other
draw family, not through the compiled-effect-specific state plumbing at all.

#### FX-062 existence-gate findings (2026-08-17)

`tools/graphics/mojoshader_gl_probe.cpp` (mirroring `mojoshader_sdlgpu_probe.cpp`'s structure and
golden-pixel methodology, linking only MojoShader and SDL3 -- no CNA, no EasyGL) proves MojoShader's
OpenGL adapter renders a committed effect correctly against a real GLES3 context. Both `--render`
technique/pass combinations land on the exact same bytes as the SDL_GPU adapter's independently
hand-verified golden pixels: `(3,6,10,13)` for `MainPixelShader` (texture sampling, a struct-driven
preshader) and `(20,41,61,82)` for `FlatPixelShader` (no sampling, an array-driven preshader). That
match is also a second, independent confirmation that FX-071's `mojoshader_effects.c` preshader fix
(the two register-count/float-count unit bugs) is correct -- both backends share that exact code.

Two real, GL-adapter-specific bugs surfaced before any of this rendered, both fixed in the probe and
both things `SdlGpuCompiledEffect`'s eventual `EasyGL` counterpart will need to get right from the
start:

1. **A calling-convention mismatch, not a MojoShader defect.** Every `MOJOSHADER_gl*` function
   relevant to `MOJOSHADER_effectShaderContext` omits the leading context-pointer argument the
   backend struct's typedefs declare -- the OpenGL adapter keeps its context as implicit
   thread-local state (`MOJOSHADER_glMakeContextCurrent`), unlike SDL_GPU/D3D11's explicit
   per-call context pointer, and `MOJOSHADER_glCompileShader` additionally has no `mainfn`
   parameter at all. Casting the real functions directly into those slots -- the natural first
   attempt, mirroring `MakeSdlBackend` -- silently misaligns every argument. It reproduced as
   `MOJOSHADER_parse()` failing with `"invalid swizzle"` on every committed effect, immediately,
   regardless of which GLSL/GLSLES/GLSLES3 profile was requested, which is what proved it was a
   calling-convention bug rather than a profile-specific codegen defect. Fixed with thin trampoline
   functions dropping the unused leading argument(s) (`CompileShaderTrampoline` and five siblings in
   the probe; `shaderAddRef`/`getParseData` are the only two slots whose real signature already
   matches, with no context argument in either form).
2. **A required, GL-only uniform the Effect Framework never populates.** The generated GLSL vertex
   shader references a `uniform float vpFlip` for the GL/D3D9 clip-space Y mismatch (plus a paired
   D3D9-`[0,1]`-to-GL-`[-1,1]` depth remap on `gl_Position.z`) that `MOJOSHADER_effectCommitChanges`'s
   ordinary register-file uniform push never touches -- a separate call,
   `MOJOSHADER_glProgramViewportInfo(viewportW, viewportH, backbufferW, backbufferH,
   renderTargetBound)`, is required after every `MOJOSHADER_glProgramReady()`, whenever the render
   target could have changed. Skipping it leaves `vpFlip` at GLSL's zero-initialized default, which
   zeroes `gl_Position.y` for every vertex and collapses any geometry to zero screen area --
   reproduced as a draw that reported no GL error at any step (valid program bound, attributes
   correctly located and enabled, no culling/depth/scissor/blend interference) yet changed zero
   pixels. Root-caused by dumping the generated GLSL source via `parseData->output` (the GL/GLSL
   profiles return readable source text here, unlike SPIR-V's binary output) and reading it, not by
   further trial-and-error state probing.

A third, unrelated bug was in the probe itself, not MojoShader: binding `colorTarget` to set up the
FBO attachment clobbered texture unit 0's binding (left over from binding `whiteTexture` there
earlier), and the render then sampled the same texture it was rendering into -- caught because
alpha came back plausible (a real preshader/uniform value) while RGB did not, which pointed at
sampled-texture content rather than the uniform/preshader pipeline already proven correct by
FX-071. Fixed by rebinding `whiteTexture` to unit 0 immediately before the draw.

**Both `EasyGLCompiledEffect` and the draw route are now done -- see the FX-062 row above.** The
prediction held: EasyGL draws immediately rather than deferring to a `Present()`-time replay the way
SDL_GPU does, so the draw route (`EasyGLRenderer::BindCompiledEffectForDrawEXT`) needed no
queue/upload/replay split and no uniform-snapshot-capture-before-overwrite concern -- nothing else
touches MojoShader's shared register files between `ApplyPass()` and the draw call in the same
synchronous call chain.

#### FX-064 existence-gate findings (2026-08-17)

`tools/graphics/mojoshader_vulkan_probe.cpp` sets out to answer a different question than the
FX-061/FX-062 probes did. Those two proved an *existing* MojoShader adapter (`mojoshader_sdlgpu.c`,
`mojoshader_opengl.c`) links a committed effect against a real device. MojoShader ships no Vulkan
adapter at all -- `mojoshader_opengl.c`, `mojoshader_sdlgpu.c` and `mojoshader_d3d11.c` exist;
there is no `mojoshader_vulkan.c` -- so this probe had to write one from scratch, against the
public `MOJOSHADER_effectShaderContext` nine-function contract, exactly the shape a real CNA
Vulkan renderer will need. In that sense the probe source itself is FX-064's actual deliverable,
not just evidence supporting a separate implementation.

Three real findings, discovered by reading the pinned MojoShader's SPIR-V emitter
(`profiles/mojoshader_profile_spirv.c`, `mojoshader_common.c`) before writing any Vulkan code, not
by trial and error against a running device:

1. **Two SPIR-V flavours share one profile-string pair, undocumented in `mojoshader.h`.**
   `MOJOSHADER_PROFILE_SPIRV` ("spirv") and `MOJOSHADER_PROFILE_GLSPIRV` ("glspirv") both compile
   through the same `emit_SPIRV_*` functions, but `emit_SPIRV_start()` sets an internal
   `ctx->spirv.mode` from *which string was passed* -- `SPIRV_MODE_VK` for "spirv",
   `SPIRV_MODE_GL` for "glspirv". Only VK mode emits genuine `SpvStorageClassUniform` blocks with
   real `SpvDecorationDescriptorSet`/`SpvDecorationBinding` decorations, at a **fixed** four-set
   layout `mojoshader_profile_spirv.h` names explicitly:
   `MOJOSHADER_SPIRV_VS_SAMPLER_SET`/`_VS_UNIFORM_SET`/`_PS_SAMPLER_SET`/`_PS_UNIFORM_SET` = 0/1/2/3
   (binding = each reflected sampler's index for the two sampler sets, binding 0 for each uniform
   set's single struct). GL mode instead decorates scalar `UniformConstant` variables with only a
   `Location` -- legal for the `GL_ARB_gl_spirv` extension `mojoshader_opengl.c`'s own SPIR-V path
   targets, illegal for real Vulkan outside opaque resource types. Getting the profile string
   backwards is the natural first mistake (SDL_GPU's own profile happens to be `"spirv"` too, so
   copying that pattern gets it right by accident; nothing else in the public API flags "glspirv"
   as GL-only).
2. **No shader-side viewport flip exists for genuine Vulkan output, and none is needed.**
   `emit_SPIRV_vs_main_end()` -- the function that multiplies `gl_Position.y` by a `vpFlip` uniform
   and remaps `gl_Position.z`'s depth range, which FX-062's GLSL text route needed
   (`MOJOSHADER_glProgramViewportInfo`) -- checks `ctx->profile_supports_glspirv` and returns
   immediately for a real `"spirv"` shader. This is not a gap; Vulkan's clip-space convention
   already matches Direct3D 9's (Y-down, depth range `[0, 1]`), unlike OpenGL's (Y-up, depth range
   `[-1, 1]`), so neither correction applies. The probe's pipeline uses a plain, non-negative-height
   `VkViewport` and renders correctly, confirming the theory against a real device rather than
   trusting the source reading alone.
3. **The public API's only route to the trailing `SpirvPatchTable`'s byte size is a return value,
   not a struct.** A `"spirv"`-profile `MOJOSHADER_parseData::output` carries a private
   `SpirvPatchTable` (declared in `mojoshader_internal.h`, which this probe deliberately never
   includes) appended after the real SPIR-V words. `MOJOSHADER_linkSPIRVShaders()` already computes
   that struct's size internally and returns it (`return sizeof(SpirvPatchTable);` in
   `mojoshader.c`) -- exactly the number a caller needs to trim `output_len` down to real SPIR-V
   word count before `vkCreateShaderModule`, with no private header required.

One thing the public linking API leaves genuinely unanswered: `MOJOSHADER_spirv_link_attributes()`
(called inside `MOJOSHADER_linkSPIRVShaders()`) assigns the vertex-output/pixel-input interface's
Location decorations, but nothing in the public path patches the vertex shader's *own* input
attribute locations -- the ones a `VkVertexInputAttributeDescription` must match -- which come out
of `MOJOSHADER_parse()` already final. Rather than reverse-engineer the exact rule from four
thousand lines of unfamiliar emitter code, the probe carries a ~40-line SPIR-V decoration scanner
(walking the standard word-stream instruction format with `spirv/spirv.h`'s own enums, the same
header the emitter includes) that reads `OpName`/`OpDecorate Location` pairs out of the finished
module directly. That confirmed vertex input locations come out in vertex-attribute declaration
order for this fixture (`POSITION0` -> location 0, `TEXCOORD0` -> location 1) -- but a real Vulkan
renderer should keep the scanner (or an equivalent), not hardcode that specific pair, since nothing
in the public API contracts the ordering.

All three technique/pass combinations of `CnaConformanceEffect.fx` rendered correctly on the first
attempt against this machine's real Intel iGPU (`Iris(R) Xe Graphics`), with the Khronos validation
layer enabled throughout and zero warnings or errors at any step: `MainPixelShader` (technique 0
pass 0, texture sampling, a struct-driven preshader) landed on `(3,6,10,13)`, and `FlatPixelShader`
(technique 0 pass 1 and technique 1 pass 0, no sampling, an array-driven preshader) landed on
`(20,41,61,82)` both times -- byte-identical to the SDL_GPU and OpenGL backends' own independently
established golden pixels for the same shaders, a third confirmation that the shared preshader
register-count fix (FX-051/FX-071 lineage) generalizes across every MojoShader output path this
project uses. Neither of those two already-fixed bugs, nor any new one, surfaced on this path.

Superseded on 2026-08-18 by `FX-065`, which turned this prototype into a real backend in
`modules/renderers/vulkan/`. Two of the probe's conclusions carried over unchanged -- the fixed
four-descriptor-set layout scheme, and the reflection-driven pipeline/layout caching it identified
as missing -- and one did not: the probe concluded that no shader-side flip machinery was needed,
which held for the probe's own hand-written geometry but not inside a renderer whose stock shaders
each carry an explicit `pos.y = -pos.y`. A compiled shader carries no such line, so the real
backend flips through a negative-height viewport instead. Cube sampling turned out to work here
rather than needing a refusal; vertex-stage sampling, multi-stream declarations and `Texture3D` on
a pixel sampler are refused by name, as predicted.

### Phase H - Optional future formats and tooling

Renumbered 2026-08-17: these three rows carried the IDs `FX-070`/`FX-071`/`FX-072`, which Phase G
already used for DirectX 9, the SDL_GPU draw route and nothing respectively. The collision was a
documentation defect, not a scheduling one -- no work referenced these IDs.

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-095 | Evaluate a separate MGFX/mgfxo reader and runtime without weakening FX bytecode diagnostics | FX-057 | Written compatibility/design decision; no format guessing |
| FX-096 | Evaluate an offline CNA effect compiler/package tool | FX-069 | Reproducible cross-platform artifact, reflection, and licensing design |
| FX-097 | Evaluate runtime `.fx` source compilation only if a concrete game-port requirement justifies it | FX-069 | Security, compiler redistribution, caching, and platform availability are resolved |

### Phase I - Silent-fallback repair and conformance widening (2026-08-17)

These close the gaps the first rollout left. Their narrative is in the repair-pass section at the
top of this file; the acceptance criteria are here.

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-080 | Give SpriteBatch a real compiled-effect route on every backend that claims the capability | FX-062, FX-071 | **Done.** EasyGL gained `EasyGLSpriteBatchRenderer::FlushBatchWithCompiledEffect`, which stages the sprite quad with its own `VertexDeclaration`, applies every pass of the current technique, and binds the drawn texture to slot 0 after each apply -- FNA's `SpriteBatch.DrawPrimitives` rule exactly. **SAMPLE-006 follow-up:** FNA first applies the stock `SpriteEffect`, so a custom XNA pass may legally replace only the pixel shader and inherit the stock vertex shader/projection; EasyGL now embeds and applies that exact XNA stock effect before the custom passes, and resolves unassigned pixel samplers from the owning `GraphicsDevice.Textures` collection (slot 0 still receives the drawn sprite after apply). A dedicated pixel-only, slot-1 sampling test pins both rules. SDL_GPU's route applies the passes itself instead of relying on the caller. Covered by the shared `RunCompiledEffectSpriteBatchContract`, which reads the rendered pixel back: a stock-shader fallback produces the sampled texture's colour, not the effect's `Tint`, so it cannot pass by accident. A backend that genuinely cannot run one must throw by name; the contract accepts a named refusal and rejects silence |
| FX-081 | Stage `DrawUser*` vertices with the vertex type's own canonical `VertexDeclaration` | FX-024 | **Done.** All fourteen staged-buffer sites in `GraphicsDevice` now call `SetVertexDeclaration(VertexT::getVertexDeclarationStatic())`, matching FNA's `VertexDeclarationCache<T>`. Renderer-neutral by construction: no backend needs a stride-to-layout guess for these draws any more. The declarations are byte-identical to the fixed-stride tables EasyGL's `ApplyLayout` and FNA3D's `ElementsForStride` already used, so stock draws are unchanged; verified by full-suite runs on three configurations |
| FX-082 | Make EasyGL's compiled route multi-stream- and instancing-aware | FX-062 | **Done.** `BindCompiledEffectForDrawEXT` takes the bound stream set, resolves every shader input across it, and binds each match from its own buffer, stride and byte offset -- `MOJOSHADER_glSetVertexAttribute` calls `glVertexAttribPointer` immediately, so the array buffer bound at that moment is what the attribute captures. `DrawInstancedPrimitivesEx` gained the compiled branch it never had, with divisors set through `MOJOSHADER_glGetVertexAttribLocation` rather than `MOJOSHADER_glSetVertexAttribDivisor` (which asserts on an ES 3 context, where the pinned adapter never sets `have_GL_ARB_instanced_arrays`). All compiled draws now share **one** renderer-owned vertex array object: MojoShader tracks enabled attribute arrays in context-global state, so routing them through each buffer's own VAO both desynchronised that belief and overwrote the stock attribute pointers `ApplyLayout` had installed there |
| FX-083 | Make an Effect's sampler state reach the GPU, or document what an API cannot express | FX-023, FX-026 | **Done.** EasyGL's compiled route applies the pass's own filter/addressing/anisotropy/LOD to the slot before drawing, and only for slots a pass actually assigned -- an unassigned slot keeps the game's selection, so `SpriteBatch.Begin`'s sampler survives. `EasyGLRenderer::ApplySamplerMipState` maps `MaxMipLevel` onto the sampler object's `GL_TEXTURE_MIN_LOD` (per slot, unlike FNA3D's per-texture `GL_TEXTURE_BASE_LEVEL`) and `MipMapLevelOfDetailBias` onto `GL_TEXTURE_LOD_BIAS` on the desktop profile only -- OpenGL ES has no such state, which is why FNA3D's own GL driver skips it too. SDL_GPU gained `ApplySamplerMipState`, real `min_lod`/`mip_lod_bias` on its sampler create info, and both in the sampler cache key. Recorded in `docs/sampler-state-support.md` §6b, including the one remaining gap: SDL_GPU's **stock** draw families still capture only filter/addressing/anisotropy into their command structs |
| FX-084 | Give the shared fixture a drawable program pair | FX-060 | **Done.** `BuildSyntheticVertexShader` hand-assembles a `vs_2_0` program and its Direct3D 9 constant table -- `oPos = mul(POSITION0 + TEXCOORD0 * StreamMix, Transform)`, the TEXCOORD term opt-in -- alongside the existing `ps_2_0`. `BuildSyntheticDrawableEffect()` binds the pair on every pass, so any pass a contract applies (including whichever SpriteBatch picks) has a shader pair. Still no compiler dependency and no committed binary: the suite stays runnable by a backend that has only its own device |
| FX-085 | Add a parameter-API conformance section | FX-016, FX-060 | **Done.** `RunCompiledEffectParameterApiContract` round-trips every reflected shape through the public XNA setters and getters: scalars (including the int- and bool-to-float conversions), vectors through both the typed and packed-array setters, matrices and both transpose variants, arrays through the whole-array setter and the per-element views, structure members, and the string semantics `FX-089` settled |
| FX-086 | Add a draw-matrix conformance section with read-back pixel checks | FX-060, FX-084 | **Done.** `RunCompiledEffectDrawContract` covers buffered non-indexed, buffered indexed, buffered indexed with a non-zero `baseVertex` **and** `startIndex`, buffered non-indexed with a non-zero `vertexStart` (both padded with junk ahead of the real data, so a route that drops either offset draws the junk), user non-indexed, user indexed, and the canonical `VertexPositionColor` overloads that take no declaration at all -- each drawing a full-target quad and reading the centre pixel back, with a different `Tint` per draw so a stale upload is visible too. `RunCompiledEffectMultiStreamDrawContract` and `RunCompiledEffectInstancingDrawContract` add the two stream shapes, each skipping on a backend that does not advertise the capability |
| FX-087 | Add an effect-switching conformance section | FX-060, FX-084 | **Done.** `RunCompiledEffectSwitchingContract` draws two independent compiled effects and a clone alternately, twice round, and requires each to render its own parameter values -- so a shared register file leaking between instances, or a clone aliasing its source, changes a pixel |
| FX-088 | Add an orientation conformance section | FX-060, FX-084 | **Done.** `RunCompiledEffectOrientationContract` draws the same asymmetric half-target quad twice, once through a stock `BasicEffect` and once through the compiled one, and requires them to paint the same half. Written after EasyGL's compiled route was found rendering vertically mirrored against every other draw it issues; verified to fail when the old behaviour is restored, not merely to pass now |
| FX-089 | Settle compiled string-parameter semantics against XNA, not against FNA's FIXME | FX-016 | **Done.** XNA's `EffectParameter.SetValue(string)`/`GetValueString()` (decompiled `Microsoft.Xna.Framework.Graphics`) reject a parameter whose `_paramType` is not `String` with `InvalidCastException`, and otherwise set/get through `ID3DXBaseEffect`. CNA now does the same for a compiled parameter; the value lives in CNA's own per-instance storage because an Effect Framework string is CPU-side reflection data no shader stage reads, so nothing has to mutate MojoShader's shared object table. A CNA-constructed stock/CNAEXT parameter keeps its lenient behaviour, which the C API's own standalone-parameter tests rely on |
| FX-090 | Restore FNA3D device creation (out of FX scope, but it blocked verifying FX on the reference backend) | - | **Done.** `plan_runtimerenderer.md` RTR-P1-D41 removed the descriptor's `prepareWindowFlags` hook in favour of static data. That was correct for the window's visual and wrong for FNA3D, whose `FNA3D_PrepareWindowAttributes()` is also where the driver is *selected*: with the hook gone nothing called it, and every `FNA3D_CreateDevice` refused. `Fna3dRenderer`'s constructor calls it before creating the device. Before this fix every FNA3D test failed at `GraphicsDevice device;` |

### Phase J - Follow-up repair: sampler identity, GPU-visible conformance, batching (2026-08-18)

A second independent audit of `FX-080`-`FX-090` reported one HIGH-severity defect and a set of
verification gaps. Verifying each of them against the tree found the reported defects real, found
three the audit had not reached -- each of them a *silent* wrong-pixels fallback on a backend
advertising `CompiledEffects` -- and found one reported defect not reproducible as described.

The organising principle of the previous pass was "the suite must prove the compiled shader is what
draws". This pass extends it: **the suite must prove the compiled Effect's texture, sampler and pass
semantics reach the GPU**, because everything in that half was previously asserted against CNA's own
state objects and could therefore break without a single test noticing.

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-091 | Replace SDL_GPU's packed sampler-cache key with a structured one | FX-083 | **Done.** The key was a hand-packed `uint64` with the 32-bit LOD bias shifted to bit 40, so its top eight bits -- an IEEE-754 float's sign and seven of its eight exponent bits -- fell off the end: `0.0`, `+/-0.5`, `+/-2.0` and `+/-8.0` produced one key and `+/-1.0`, `+/-4.0`, `0.25` produced another, and each family was served the first native sampler ever built for it. `MaxMipLevel` was masked to eight bits on top of that. It is now `SamplerCacheKeyEXT`, a struct with defaulted member-wise equality and an FNV-1a hash over every member, covering filter, addressU, addressV, **addressW**, maxAnisotropy, maxMipLevel and the bias's exact bit pattern; `addressW` is carried even though this renderer's 2D-only compiled sampling cannot observe it, so adopting the axis later cannot be handed a sampler built for a different W mode. Pinned by `SdlGpuSamplerCacheKeyTest` (four cases: the aliasing bias list, signed zero and NaN determinism, one-field-at-a-time identity across all nine filters and all three modes per axis, and `MaxMipLevel`'s full signed range) |
| FX-092 | Stop a compiled Effect's sampler state leaking into later stock draws | FX-083 | **Done, and wider than reported.** EasyGL's `samplers_[slot]` is one long-lived GL sampler object mutated in place, so every property `ApplySamplerState` did not write survived from whoever wrote it last -- and `ApplySamplerMipState` writes three of them. `ApplySamplerState` now establishes the sampler's **complete** state from its own arguments: min/mag filter, wrap S/T/R, anisotropy, `GL_TEXTURE_MIN_LOD`, `GL_TEXTURE_MAX_LOD`, `GL_TEXTURE_LOD_BIAS` and `GL_TEXTURE_COMPARE_MODE`. EasyGL also adopted `ApplySamplerAddressW` (`GL_TEXTURE_WRAP_R`), which was previously accepted and dropped. The same leak shape was then found on **FNA3D**, which the audit had not reported: its stock `SpriteBatch` flush copied `samplerStates_[0]` and overrode only filter and addressing, inheriting `maxAnisotropy`/`maxMipLevel`/`mipMapLevelOfDetailBias` from whatever wrote the slot last. FNA has no such inheritance -- `PrepRenderState` assigns the batch's whole `SamplerState` to slot zero -- so the sprite sampler is now built from the batch's state plus `SamplerState`'s own defaults. Pinned by `RunCompiledEffectStockDrawIsolationContract`, which clamps a compiled Effect to mip level 1, draws, then draws the same mipmapped texture through the stock sprite program and requires the base level back |
| FX-093 | Give the shared fixture a pixel shader that actually samples | FX-084 | **Done.** `BuildSyntheticSamplingEffect` emits `oC0 = tex2D(FxSampler, TEXCOORD0) * Tint` with the vertex shader forwarding TEXCOORD0 to `oT0`. Every Direct3D 9 token in it (`dcl t0`, `dcl_2d s0`, `texld`, `mul`, and the vs `mov oT0.xy, v1`) was checked against `fxc`'s own output for `CnaConformanceEffect.fx`'s `MainPixelShader`, which is the same shape, so the fixture stays compiler-free without being guesswork. Before this, every drawable fixture had no sampler at all, so the entire texture/sampler half of a compiled Effect could break with the draw suite still green |
| FX-094 | Make two passes of the shared fixture GPU-observably different | FX-084 | **Done.** A drawable fixture now carries a second pixel shader, `oC0 = Tint.yzxw`, and gives it to pass `P0` alone; `StatePass` and `P1` keep the unrotated one. One channel rotation is enough to tell them apart and costs one extra object. `RunCompiledEffectPassSelectionContract` draws each pass in turn, returns to the first, and selects a second technique's pass by name. Before this every pass of a drawable fixture bound the identical program pair, so a backend that applied pass 0 where the contract asked for pass 1 -- or fell back to "the first pass" when it could not resolve one -- rendered identical pixels and passed |
| FX-098 | Stop EasyGL's compiled draws silently running the stock program | FX-062 | **Done. Not reported by the audit; found while building the fixture above.** `MOJOSHADER_glBindProgram` shadows the bound GL program (`if (program == ctx->bound_program) return;`) and `MOJOSHADER_glProgramReady` never calls `glUseProgram` at all. So the moment anything else in the renderer bound its own program -- every stock 3D draw's `p.prog.use()`, every SpriteBatch flush's `program_.use()`, and even the `program_.use()` inside `EasyGLSpriteBatchRenderer`'s **constructor** -- MojoShader still believed its program was current and every later compiled draw in the process ran the stock program instead, permanently, with no diagnostic. Merely constructing a `SpriteBatch` was enough. `BindCompiledEffectForDrawEXT` now bounces the bound pair through `MOJOSHADER_glBindShaders(nullptr, nullptr)` and back, which is the public API for "assume nothing about the GL program"; the pair comes from the linker cache, so nothing is recompiled and the program's refcount goes 2 -> 1 -> 2 with the cache's own reference holding it. Pinned by `RunCompiledEffectSwitchingContract`, which now interleaves a stock `BasicEffect` draw and a stock `SpriteBatch` draw with compiled ones in both directions -- something its own doc comment already claimed and its code did not do |
| FX-099 | Let a compiled Effect sample a `RenderTarget2D`, the right way up | FX-062, FX-071 | **Done. The audit's F-2 as reported was NOT reproduced; the underlying gap was worse.** The claim was that EasyGL silently samples a render target mirrored. It did not: `EasyGLCompiledEffect::SetParameterTexture` **refused a `RenderTarget2D` outright** (`AsEasyGLTexture` recognised only `EasyGLTextureRenderer`), so the most ordinary use a compiled Effect has -- post-processing a scene the game just drew -- could not be expressed at all. SDL_GPU refused it identically. Both resolvers now accept a render target. On SDL_GPU nothing else was needed: its targets store rows the same way up as an uploaded texture. On EasyGL they do not, and MojoShader's generated GLSL carries none of this renderer's own `uRtFlipV`/`cnaSampleUV` sampling-time correction and cannot be made to -- so `AcquireCompiledEffectFlippedSourceEXT` blits the source's colour texture into a per-slot copy with the destination Y range reversed and binds the copy. Applied only where `SampledRowOrderIsBottomUp()` is true, so an ordinary `Texture2D` is never flipped; reallocated only when the source's extent or level count changes; refused by name on the ES 2 profiles, which have no `glBlitFramebuffer`, and when the source is the target currently being drawn into. Pinned by `RunCompiledEffectRenderTargetSourceContract` (stock baseline, one hop, two hops, and a plain `Texture2D` that must not be flipped), verified to fail when the blit's Y reversal is removed |
| FX-100 | Size FNA3D's SpriteBatch projection from the bound render target | - | **Done. Out of FX scope, but it blocked two of this pass's contracts.** `Fna3dSpriteBatch`'s pixel-space projection came from `GetViewportSize()`, which is the renderer-wide LOGICAL extent and always reported the back buffer. A stock `SpriteBatch` drawing into a `RenderTarget2D` smaller than the window therefore projected its pixels into a fraction of clip space: for an 8x8 target against the test window it rasterized nothing at all, silently. EasyGL and SDL_GPU both size this from the bound target already. `GetViewportSize` keeps its meaning (input coordinate transforms read the same method); the sprite projection reads `boundTargets_` when one is bound |
| FX-101 | Make a repeated `EffectPass.Apply()` re-establish its own pass state on FNA3D | FX-071 | **Done. Not reported by the audit.** CNA cleared its `MOJOSHADER_effectStateChanges` before every `FNA3D_ApplyEffect`. MojoShader writes that struct only in `effectBeginPass`, and FNA3D reaches that only when the effect, technique or pass CHANGES -- re-applying the same pass takes the `MOJOSHADER_effectCommitChanges` shortcut, which leaves the struct holding the pass's own pointers. FNA depends on exactly that: it allocates the struct once per Effect and never clears it. Clearing it made every repeat application publish nothing -- no render states, no sampler states, no texture binding -- so the second and every later `Apply()` of one compiled sampling effect silently dropped its own sampler binding and the draw that followed rendered nothing. Reproduced with two consecutive draws of one effect, which produced the clear colour from the second onwards; a game holding one Effect across a frame is the normal case, not an edge one. Pinned by a repeat-apply section in `RunCompiledEffectSamplerPixelContract` |
| FX-102 | Give SDL_GPU's SpriteBatch XNA's own compiled-Effect batching granularity | FX-071, FX-080 | **Done.** XNA runs a compiled Effect's passes at flush granularity over a contiguous run of same-texture sprites (`SpriteBatch.FlushBatch` splits the runs, `DrawPrimitives` wraps each run's draw in `foreach (pass)`), so for two sprites sharing a texture the order is pass-major. SDL_GPU applied the passes inside `Draw()` and queued the sprite once per pass, which is sprite-major -- a different image under any order-dependent blend. Sprites are now recorded until the flush and replayed run-major then pass-major; `SpriteSortMode::Immediate` is deliberately NOT routed through it, because XNA flushes per `Draw()` there and the per-sprite loop is already the right order. Pinned by `RunCompiledEffectSpriteBatchMultiPassContract`, which uses `BlendState.NonPremultiplied` and two overlapping half-transparent sprites so the two orders differ by 192 against 160 out of 255; verified to produce exactly 160 when the fix is reverted |
| FX-103 | Test that a sprite's own texture wins slot 0 | FX-080 | **Done.** FNA's `SpriteBatch.DrawPrimitives` sets `GraphicsDevice.Textures[0] = texture` immediately after `pass.Apply()` ("Set this _after_ Apply, otherwise EffectParameters override it!"). `RunCompiledEffectSpriteBatchTextureSlotContract` gives the effect's own texture parameter a red texture and draws a green/blue sprite, so which one reached the sampler is the read-back pixel; the same batch also checks that a source rectangle and `SpriteEffects.FlipHorizontally` reach the shader as texture coordinates, which is only observable once something samples |
| FX-104 | Add the positive half of the compiled string-parameter contract | FX-089 | **Done.** `BuildSyntheticStringParameterEffect` declares `Caption`, a real Effect Framework string object with an initial value. The parameter contract now checks the reflected type and class, the initial value, set/get, the empty string as a value in its own right, clone independence in both directions, and that applying an effect carrying one uploads nothing |
| FX-105 | Reject the numeric accessors on an object parameter | FX-104 | **Done (2026-08-18).** XNA rejects `GetValueSingle()`/`SetValue(float)` on a String parameter with `InvalidCastException`; CNA's numeric accessors reached the compiled byte storage with no type check. The guard is `RequireNumericParameter`, and it is written against the parameter **class** rather than the String type alone, because the defect is the same for every object parameter: a compiled effect stores an object-table INDEX at their byte offset, so a numeric getter returned that index reinterpreted as a number and a numeric setter overwrote it, detaching the parameter from its object. Applied to all 36 numeric accessors -- every getter, setter, array form and transpose variant. Deliberately narrow in two ways: `Struct` is NOT refused (it has real numeric storage and its members are reachable through `StructureMembers`), and a CNA-constructed stock/CNAEXT parameter is not either, since it has no compiled storage -- the same carve-out `RequireStringParameter` already makes for the C API's standalone-parameter tests. Asserted in the shared parameter contract on a String parameter, a texture parameter, and the negative cases (a scalar, a matrix and a struct member must all still work) |
| FX-106 | Cover per-stream offsets and the instancing edges | FX-082, FX-086 | **Done.** The multi-stream contract now binds each stream at a DIFFERENT non-zero `VertexOffset` and draws from a non-zero `vertexStart`, with junk in front of each buffer's real data, so ignoring the offsets, applying one stream's to both, or folding them and losing the remainder all read junk. The instancing contract adds a non-zero `baseVertex`, `startIndex` and instance-stream `VertexOffset`, and then an ordinary non-instanced compiled draw that must read its second stream per vertex -- a divisor left at 1 on the shared array object makes the whole quad stop depending on that stream |
| FX-107 | Rebuild the compiled route's GL objects across a context recreation | FX-082 | **Done, as an explicit refusal plus a real reset.** The compiled route owns two GL objects outside easy-gl's recovery registry: the shared vertex array object and the per-slot flipped-source copies. `compiledEffectVaoCreated_` stayed true across a recreation while the name behind it died with the old context, so every later compiled draw bound array object 0 and rasterized nothing. Both are now dropped (handles only, no GL call) inside the context-loss transaction. The deeper half is refused rather than faked: MojoShader's context owns the linked programs for every live compiled Effect, rebuilding them means rebuilding the effects from bytecode the game no longer holds, so `RequireCompiledEffectContextEXT` throws by name once the meta-gl context generation moves past the one MojoShader's context was made in. Pinned by `EasyGLCompiledEffectDrawTest.CompiledDrawObjectsSurviveAContextRecreation`, which accepts either a correct draw or a named refusal and rejects silence |
| FX-108 | Name the 3D/cube and vertex-stage refusals by shape, and classify them | FX-062, FX-071 | **Done.** EasyGL's pixel-sampler refusal used to fold three different situations into one message ("an unbound slot, or a 3D/cube texture"). It now names `Texture3D` and `TextureCube` separately, says that the renderer samples both elsewhere, and says the limitation is specific to compiled Effects; SDL_GPU's does the same. The vertex-stage refusal is explicitly marked as the OTHER kind -- renderer-wide, because no renderer consumes the public vertex-sampler surface at all (`FX-109`). See the limitation table in section 10.5 |
| FX-109 | Decide what `GraphicsDevice.VertexTextures`/`VertexSamplerStates` are | FX-026 | **Open, classified.** Verified: `IGraphicsRenderer` has no vertex-sampler or vertex-texture hook of any kind, and `GraphicsDevice` never pushes either collection anywhere. Both are public XNA API that reaches no renderer -- a **renderer-wide** unimplemented feature, not a compiled-Effect one. The single exception is FNA3D's compiled effect, which reads the effect's OWN vertex sampler assignments and calls `FNA3D_VerifyVertexSampler` directly, bypassing the public collections entirely. So compiled Effects are the only place vertex-stage sampling works at all in CNA today, and only on FNA3D. Implementing the public surface means a new renderer hook plus per-renderer support and is out of this pass's scope |
| FX-110 | Implement compiled-Effect 3D/cube sampler bindings | FX-108 | **Cube done on all three backends (2026-08-18); volume done on FNA3D and refused with a recorded reason on the other two.** The fixture gained `SyntheticSamplerKind`, which drives the pixel shader's `dcl_<2d\|cube\|volume>` texture-type field (verified against a real fxc-compiled cube declaration in `EnvironmentMapEffect.fxb`: usage token `0x98000000`, type 3), the width of the coordinate the vertex shader forwards, and the reflected object types the effect declares. `RunCompiledEffectCubeAndVolumeSamplerContract` binds a cube whose six faces are six colours and samples four directions, then a volume whose two depth slices are two colours and samples both -- and finally requires a `Texture2D` bound to a `samplerCUBE` to be REFUSED, because on every one of these APIs a mismatched dimension samples black rather than erroring. Per backend: **FNA3D** samples both cube and volume, and gained the missing dimension check on `SetParameterTexture` (it silently bound the wrong kind before). **EasyGL** samples cube; `ResolveSamplerTexture` resolves all three kinds through their own renderer interfaces, which are three unrelated types rather than a hierarchy, and the reflected `MOJOSHADER_sampler.type` is checked against the bound kind. **SDL_GPU** samples cube through `ResolveSampledCubeEXT` and carries the same dimension check. Two named refusals remain, each with its reason: EasyGL cannot create a **volume**-sampling effect at all on the OpenGL ES profiles, because pinned MojoShader's GLSL ES output declares `uniform sampler3D` with no precision qualifier and GLSL ES 3.00 has no default precision for that type (`error: No precision specified in this scope for type 'sampler3D'`) -- a third-party output limitation, not a CNA one, and refused cleanly at effect creation; SDL_GPU cannot bind one because `SdlGpuTexture3DRenderer` keeps its native handle as a bare pointer rather than the lifetime-tracked `SdlGpuSampledTextureEXT` a deferred replay needs, which is a texture-lifetime task rather than an FX one. **Vulkan (added 2026-08-18 with `FX-065`, completed by `FX-112`)** samples BOTH cube and volume. Cube goes through `IVulkanCubeSamplable`; volume needed only an accessor, which is worth recording because the first reading of it was wrong: `VulkanTexture3DRenderer`'s image is already created `VK_IMAGE_USAGE_SAMPLED_BIT` with a `VK_IMAGE_VIEW_TYPE_3D` view, and its `SetData` already leaves the image in `SHADER_READ_ONLY_OPTIMAL` -- everything needed to sample it existed, and only the view was unreachable. A new `IVulkanVolumeSamplable` exposes it, kept as its own interface rather than folded into `IVulkanSamplable` because a `VkImageView` carries its own view type and the three kinds must stay distinguishable by type rather than by convention. The dimension check against the reflected `MOJOSHADER_sampler.type` applies to all three. **Reclassification (2026-08-18):** SDL_GPU's refusal is renderer-wide, not compiled-Effect-specific as this row previously implied. `SdlGpuTexture3DRenderer` appears in that renderer only at creation, `SetData` and `GetData` -- SDL_GPU samples a `Texture3D` in NO route, stock or compiled -- so binding one for a compiled effect would be adding a renderer capability, not repairing an FX gap. The lifetime observation (a bare `SDL_GPUTexture*` rather than the shared-ownership `SdlGpuSampledTextureState` the other kinds use) is accurate and is what such a task would start from. EasyGL likewise carries no CNA-authored volume refusal at all: its compiled route resolves and binds all three kinds, and only the GLSL ES profiles fail, inside MojoShader's own emitted source |
| FX-113 | Restore FNA's null compiled-sampler behavior on EasyGL | FX-080, FX-110 | **Done (2026-08-24, found by SAMPLE-014 Spacewar).** Spacewar's original `ship.fx` intentionally assigns its reflection texture for ships and assigns `null` for the non-reflective shapes. FNA's `Effect.INTERNAL_updateSamplers` publishes only a non-null parameter texture; when the resulting `GraphicsDevice.Textures` slot is null, FNA3D's OpenGL `VerifySampler` binds texture zero to the slot's target and returns. EasyGL instead threw a fatal exception whenever a reflected pixel sampler resolved to no texture. It now distinguishes a valid null slot from a non-null texture owned by another backend, explicitly unbinds the reflected 2D/cube/volume target for the valid-null case, restores texture unit zero, and continues the draw. `EasyGLCompiledEffectDrawTest.AShaderMaySampleANullTextureLikeFnaOpenGL` pins the no-throw draw through a real sampling effect; Spacewar supplies the end-to-end native/WebGL2 case. No dummy texture or sample-side substitution was added |
| FX-114 | Retain EasyGL compiled-sampler resources across C++ value-wrapper replacement | FX-080, FX-110, FX-113 | **Done (2026-08-24, found by SAMPLE-014 Spacewar).** XNA textures are managed references, while CNA currently maps `Texture2D` and `TextureCube` to copyable value wrappers sharing one renderer resource. Faithful Spacewar code calls cached `Content.Load<TextureCube>()` from `Render` and replaces that local wrapper; EasyGL's compiled-effect binding kept only the old wrapper address and later rejected/dereferenced it even though the native cube remained alive in `ContentManager`. Each non-null sampler assignment now captures strong renderer-resource ownership for 2D, 3D or cube textures and the draw binds that stable resource directly. A later null parameter deliberately preserves it, matching FNA's unchanged `GraphicsDevice.Textures` slot. The stock `EnvironmentMapEffect.fxb` cube sampler pins the lifetime regression. No sample-side load hoist or dummy texture was added |

## 9. Required test matrix

Every renderer that claims `CompiledEffects` must run the following shared categories:

| Category | Required observations |
|---|---|
| Constructor/format | valid bytecode, empty, random, every truncation boundary, oversized, MGFX |
| Reflection | order and name lookup; semantic/class/type; rows/columns; arrays; structures; annotations; defaults |
| Parameter access | bool/int/float, scalar/vector/matrix, transpose rules, arrays, subviews, strings, textures, invalid types/counts |
| Techniques/passes | first technique, selection, ownership, exact pass index, multi-technique/multi-pass pixels |
| States | blend, depth, stencil, cull/fill, color masks, multisample, sampler filters/addressing/LOD/anisotropy |
| Drawing | user/buffered, indexed/non-indexed, SpriteBatch, render targets, multi-stream, instancing, texture switching |
| Clone | values/textures copied, technique preserved, independent mutation/application/disposal |
| Lifecycle | effect before/after device disposal, selected-effect disposal, reset/recreate, partial construction failure |
| Compatibility | normalized FNA reflection/state output and golden pixels |
| Regression | all stock effects and CNAEXT `ShaderEffect` retain their prior behavior |
| Robustness | fuzz corpus, ASan, UBSan, leak checking, deterministic error messages |

Golden images alone are insufficient. Reflection/state oracle comparisons catch errors that happen
to render the same pixels, while pixels catch native binding and pipeline failures that reflection
alone cannot see.

**Where each category is enforced (2026-08-17).** The table above was aspirational when written;
`FX-084`-`FX-088` moved most of it into `tests/support/CNA/TestSupport/CompiledEffectConformance.hpp`,
which is the file a backend must pass before `CompiledEffects` may be true.

| Category | Enforced by |
|---|---|
| Constructor/format | `RunCompiledEffectFormatContract` |
| Reflection | `RunCompiledEffectReflectionContract` |
| Parameter access | `RunCompiledEffectParameterApiContract` |
| Techniques/passes | `RunCompiledEffectTechniqueContract`, plus the draw sections' own pass selection |
| States | `RunCompiledEffectRenderStateContract`, `RunCompiledEffectStatePolicyContract`, `RunCompiledEffectSamplerContract` |
| Drawing | `RunCompiledEffectDrawContract` (buffered/user, indexed/not, non-zero base/start offsets, built-in vertex types), `...MultiStreamDrawContract`, `...InstancingDrawContract`, `...SpriteBatchContract`, `...OrientationContract` |
| Clone | `RunCompiledEffectCloneContract`, and the clone leg of `RunCompiledEffectSwitchingContract` |
| Lifecycle | `RunCompiledEffectLifecycleContract` |
| Compatibility | FNA3D's own `StockFixtureReflectionMatchesTheFnaOracle` and `Fna3dEffectStateOracleTest` -- not portable, so backend-local |
| Regression | each backend's full `CnaTests` run |
| Robustness | `FX-051`'s fuzz harness and corpus, `FX-052`'s sanitizer run -- FNA3D-local |

Two categories stay deliberately backend-local rather than shared: the FNA oracle comparison needs
FNA itself, and the fuzz/sanitizer gates need a driver to run against. A new backend inherits the
fourteen shared sections and is expected to add its own golden pixels on top, not instead.

## 10. Milestones and definitions of done

### 10.1 FNA3D usable MVP

The feature may be advertised as usable on FNA3D only when all of the following are true:

- direct byte-array construction and XNB `EffectReader` loading work;
- public reflection matches the FNA oracle for all committed fixtures;
- parameter mutation, annotations, textures/samplers, techniques, passes, and state changes work;
- SpriteBatch and general 3D draw paths pass deterministic pixel tests;
- clone and device/effect lifetime tests pass independently;
- malformed input and unsupported renderers fail explicitly and safely;
- fuzz/sanitizer and full regression gates are clean;
- `CompiledEffects` is true only on FNA3D and documentation says so precisely.

#### Assessment (updated 2026-08-15)

`FX-057` is **declared**. All eight criteria pass.

The two that took longest are worth recording, because both were once described here as blocked on
something outside the project's control, and neither turned out to be.

The oracle criterion was blocked on having no independent implementation to compare against. It
closed once `mono` and the June 2010 DirectX SDK's `fxc` were available: CNA now has a conformance
source it controls, compiled by the same Effect compiler XNA's Content Pipeline used, and FNA's own
reflection of every committed binary checked in as test data.

The fuzz gate was the last, and it was not a formality. The campaign found **forty-one distinct
ways untrusted bytecode crashed the process**. Forty were in the pinned MojoShader and are fixed by
the managed patch; the forty-first was CNA's own, and is the one worth remembering -- the
sampler-texture map selected parameters by sampler *type* where the storage layout follows the
*class* first, so it read a small value buffer as an array of much larger structures. CNA had every
fact needed to avoid it.

What the gate does and does not establish should be stated plainly. It is a measured bound, not a
proof: one million coverage-guided executions per FNA3D driver, from the committed seed corpus,
under AddressSanitizer, with asserts fatal, producing no new artifact. That bar was written down
before it was met. It was met with margin -- 3,079,834 executions on OpenGL/GLSL and 2,669,555 on
SDL_GPU/SPIR-V, both full windows, zero findings. Fuzzing cannot prove absence, so the porter guide
continues to state the trust boundary rather than promise safe failure on arbitrary hostile content.

| Criterion | State |
|---|---|
| byte-array and XNB loading | **Pass** -- both paths tested, including an end-to-end ContentManager load and draw |
| reflection matches the FNA oracle | **Pass, for the six stock fixtures.** `tools/fna-reference --effects` emits FNA's own reflection and `StockFixtureReflectionMatchesTheFnaOracle` compares CNA's against it -- parameter order, names, semantics, classes, types, row/column counts, annotations, array elements, structure members, technique and pass names all agree. The synthetic fixtures still have no oracle, because no compiler produced them; the one deliberate divergence, structure member values, is recorded below |
| parameters, annotations, textures/samplers, techniques, passes, states | **Pass** -- covered by the shared conformance suite and the FNA3D-specific tests |
| SpriteBatch and 3D pixel tests | **Pass** -- deterministic pixels for both paths and for a blend-factor state oracle |
| clone and lifetime | **Pass** -- clone chains, device reset, disposal ordering, repeated cycles |
| malformed input and unsupported renderers fail explicitly and safely | **Pass, to the bound the gate measures.** Unsupported renderers refuse by name; CNA's own layer rejects every malformed category tested; and the forty-one crash classes the campaign found are fixed, kept as replayed artifacts, and no longer reachable. "Safely" holds as far as five and a half million coverage-guided executions can establish it, which is a bound rather than a guarantee -- the trust boundary stays documented |
| fuzz/sanitizer and regression gates clean | **Pass.** Sanitizers clean for CNA-owned code (`FX-052`), full regression explained (`FX-054`), and the fuzz gate met at its documented bar on both FNA3D drivers |
| `CompiledEffects` true only on FNA3D, documented precisely | **Pass at the time (2026-08-15).** Superseded: SDL_GPU (`FX-061`/`FX-071`) and EasyGL (`FX-062`) later passed the same gate and are true behind their own opt-in build options. Section 10.2's own table is the current answer |

So the accurate public statement today is: compiled effects are usable on FNA3D -- for content a
game ships and, to the bound above, for content it does not trust -- covered by a portable
conformance suite, checked against FNA's own reflection, and documented. The claim covers FNA3D and
nothing else.

**Amended 2026-08-17.** Two things about that statement aged badly and are corrected here rather
than left standing. First, it is no longer FNA3D-only: SDL_GPU and EasyGL passed the same gate.
Second, and more importantly, the gate itself was weaker than this assessment implied -- the
`FX-060` suite contained no draw at all, so "passes the shared contract" did not mean "draws with
the compiled shader". `FX-080`-`FX-088` fixed both the routes that were silently substituting a
stock shader and the suite's inability to notice. The FNA3D criteria above still hold; they are now
checked by a suite that could actually have failed them.

### 10.2 Project-wide completion

The project-wide gap is closed only after every renderer has either passed the shared conformance
suite and enabled `CompiledEffects`, or has an explicit documented unsupported rationale aligned
with that renderer's purpose. FNA3D support alone is a valuable production milestone, but must not
be presented as universal CNA renderer support.

#### Assessment (2026-08-17, re-confirmed and extended 2026-08-18)

**Not yet closed**, and the plan says so rather than declaring victory on four backends. The
2026-08-18 closure pass added the fourth (Vulkan) and stopped there deliberately: the three that
remain are DirectX 11, DirectX 9 and Metal, and not one of them can be built, run or verified on
the Linux machine this work happens on. Writing them blind behind a capability gate is exactly what
this section forbids -- a backend is promoted on executed evidence or not at all.

| Renderer | `CompiledEffects` | Evidence |
|---|---|---|
| FNA3D | true | 47/47. Full shared suite including every draw section; multi-stream and instancing actually run (it advertises both); FNA reflection/state oracles; fuzz and sanitizer gates |
| SDL_GPU (`CNA_SDL_GPU_COMPILED_EFFECTS=ON`) | true | 33 passed, 3 skipped. Multi-stream: renderer-wide (advertises no `MultiStreamVertexInput`). Instancing: renderer-wide, and now skipped on the renderer's OWN named refusal rather than on a capability that did not describe the shape (`FX-112`). Volume sampler: renderer-wide -- SDL_GPU samples a `Texture3D` nowhere at all, in any route (`FX-110`) |
| EasyGL family (`CNA_EASYGL_COMPILED_EFFECTS=ON`) | true | 27 passed, 1 skipped. Volume sampler on the GLSL ES profiles only, and the refusal comes from MojoShader's own emitted source (`No precision specified in this scope for type 'sampler3D'`), not from CNA -- CNA's compiled route resolves and binds all three texture kinds (`FX-110`) |
| Vulkan (`CNA_VULKAN_COMPILED_EFFECTS=ON`) | **true** | 24 passed, 1 skipped. `FX-065`/`FX-112`: CNA's own MojoShader SPIR-V backend, a pipeline per linked shader pair and vertex layout, the profile's four fixed descriptor sets, growable per-frame uniform chunks for a `Present()`-deferred replay, compiled instancing, and pixel-stage cube AND volume sampling. The single skip is multi-stream vertex input, which is renderer-wide (`MultiStreamVertexInput` false for stock draws equally, REMED-GFX-201) rather than an FX gap |
| DirectX 11 (`FX-063`) | false | Not implemented. Requires an executable Windows environment; see the requirements note on `FX-063` |
| DirectX 9 (`FX-070`) | false | Not implemented. Requires an executable Windows (or DXVK-native) environment; see the requirements note on `FX-070` |
| Metal (`FX-066`) | false | Not implemented. Requires macOS; see the requirements note on `FX-066` |
| SDL_GPU / EasyGL / Vulkan with the option off | false | The runtime is not compiled in; the capability reports false and construction refuses by name |
| every other identity | false | Section 10.3: planned, assessed-feasible, or unsupported by design |

What is still open before 10.2 can close (updated 2026-08-18):

- **DirectX 11 (`FX-063`), DirectX 9 (`FX-070`) and Metal (`FX-066`) are unwritten**, and not one
  of the three can be built, run or verified on this Linux machine. Their rows carry a concrete
  requirements note each, written for whoever picks them up on a machine that can execute them.
- `FX-069`'s final cross-renderer matrix depends on those three and on nothing else.
- A compiled effect whose **vertex shader samples a texture** is refused on SDL_GPU, EasyGL and
  Vulkan. This is renderer-wide, not compiled-Effect-specific: `IGraphicsRenderer` has no
  vertex-sampler hook of any kind and `GraphicsDevice.VertexTextures`/`VertexSamplerStates` reach no
  renderer at all (`FX-109`). FNA3D's compiled effect is the only place vertex-stage sampling works
  in CNA today, and it does so from the effect's own assignments, bypassing the public collections.
- A compiled draw after a GL context recreation is refused by name on EasyGL (`FX-107`).
- SDL_GPU cannot bind more than one vertex stream, has no instanced draw path, and samples a
  `Texture3D` in no route at all. All three are renderer-wide gaps, not compiled-effect ones.
- Vulkan cannot bind more than one PER-VERTEX stream (`MultiStreamVertexInput` false, REMED-GFX-201).
  Renderer-wide: its stock pipelines derive their input elements from a byte stride. Its compiled
  route would not need that limitation lifted for its own sake -- it builds vertex input from the
  declarations -- but `GraphicsDevice` gates `SetVertexBuffers` on the renderer-wide capability, so
  the two have to be lifted together and in that order.
- A **volume** sampler is refused on SDL_GPU (renderer-wide: no volume sampling anywhere) and on
  EasyGL's GLSL ES profiles only (MojoShader's own emitted source, not CNA's code). Cube and volume
  both work on FNA3D and Vulkan (`FX-110`).
- `MipMapLevelOfDetailBias` is unrepresentable on the OpenGL ES profiles, and `AddressW` on the
  OpenGL ES 2 / WebGL 1 profiles. Both renderer-wide API limits, not gaps.
- (closed 2026-08-18) `EffectParameter`'s numeric accessors now reject an object parameter
  (`FX-105`); Vulkan's draw route, compiled instancing and volume sampling (`FX-065`, `FX-112`);
  the shared suite's instancing gate no longer excuses renderers that can run the shape (`FX-112`);
  the test harness's assertion policy is non-interactive (`FX-111`).

Closed since the previous assessment: `AddressW` is now consumed by EasyGL as well as FNA3D
(`FX-092`), and a `RenderTarget2D` is accepted as a compiled sampler's source on all three
backends (`FX-099`).

Section 10.5 carries the same information as a single table, classified by limitation kind.

### 10.3 Per-renderer assessment (`FX-067`, 2026-08-15)

Every renderer identity is classified below as **planned**, **assessed feasible**, or
**unsupported by design**. No renderer is left implicit, and none silently falls back to a stock
shader: a renderer that does not opt in through `SupportsCompiledEffects()` refuses a compiled
`Effect` by name.

Two facts from the pinned toolchain drive nearly every verdict, and both were measured rather than
assumed.

**What the pinned MojoShader can emit.** Its `CMakeLists.txt` disables `ARB1`, `ARB1_NV`,
`BYTECODE` and `D3D`; enables `GLSL`, `GLSL120`, `GLSLES`, `GLSLES3`, `SPIRV` and `GLSPIRV`
unconditionally; and gates `HLSL` behind Windows/DXVK and `METAL` behind Apple. So the portable
outputs are GLSL in four dialects and SPIR-V; HLSL and Metal Shading Language exist only on the
platform that needs them.

**What it can bind for you.** Translation is not the whole job -- something has to attach the
translated shader to a pipeline and route uniforms. MojoShader ships that glue for exactly three
APIs: `mojoshader_opengl.c`, `mojoshader_sdlgpu.c` and `mojoshader_d3d11.c`. Everywhere else the
renderer writes its own binding layer, which is the real cost driver and is why Vulkan and Metal
have their own prototype tasks rather than being folded into a single "SPIR-V works" claim.

**What every backend must supply regardless.** `MOJOSHADER_effectShaderContext` is nine function
pointers -- compile, addref, delete, getParseData, bind, getBound, map/unmap uniform memory, and
error -- and the parser refuses to run without them. `cna_mojoshader_effect_probe` implements the
smallest set that parses the committed fixtures, so the size of that obligation is measured rather
than guessed: it is the floor for every row below.

#### Planned backends

| Renderer | Route | Task |
|---|---|---|
| FNA3D | Done. MojoShader effect runtime inside FNA3D, GLSL or SPIR-V chosen by FNA3D's own driver. Passes the widened FX-060 suite in full, multi-stream and instancing included | `FX-031`–`FX-038`, `FX-090` |
| SDL_GPU | Done. SPIR-V profile plus the `mojoshader_sdlgpu.c` adapter; ordinary and indexed 3D draws and SpriteBatch all have a working route, a golden-pixel test and the widened FX-060 suite passing, `SupportsCompiledEffects()` true. Multi-stream and instancing skip: the renderer advertises neither capability | `FX-061`, `FX-071`, `FX-080`, `FX-083` |
| EasyGL and the OpenGL/OpenGL ES family | Done. GLSL/GLSLES/GLSLES3 profiles plus `mojoshader_opengl.c`. One implementation serves `OPENGLES2`, `OPENGLES3`, `OPENGL33`, `OPENGL4`, `WEBGL1` and `WEBGL2`, since EasyGL is their shared implementation. Ordinary, indexed, instanced, multi-stream and SpriteBatch draw routes, a golden-pixel test and the widened FX-060 suite passing in full, `SupportsCompiledEffects()` true | `FX-062`, `FX-080`, `FX-082`, `FX-083`, `FX-088` |
| DirectX 11 | HLSL profile plus `mojoshader_d3d11.c`, Windows-only by the pin's own gating | `FX-063` |
| Vulkan | SPIR-V profile, **no adapter** -- descriptor layout, uniform buffers and vertex linkage are CNA's to write, which is why it is split into a prototype and a completion task. Prototype done: a hand-rolled backend renders real golden pixels against a real device, validation-clean | `FX-064`, `FX-065` |
| Metal | Metal profile emits MSL source and CNA's Metal renderer already builds pipelines from MSL through `newLibraryWithSource`, but there is no adapter and the profile only exists on Apple | `FX-066` |
| bgfx | Held false until a reproducible bgfx-native shader packaging route is proven; bgfx consumes its own `shaderc` container rather than any MojoShader output | `FX-068` |

#### DirectX 9 -- the case worth calling out

`DIRECTX9` is the best structural fit of any renderer in the project, better than FNA3D's, and it
needs no shader translation whatsoever.

A compiled XNA effect *is* Direct3D 9 shader bytecode. CNA's DirectX 9 renderer already hands raw
DWORD token blobs to `IDirect3DDevice9::CreateVertexShader` / `CreatePixelShader` and sets
constants through `SetVertexShaderConstantF` / `SetPixelShaderConstantF`
(`modules/renderers/directx9/src/D3D9EffectRenderer.cpp`). That is exactly the shape MojoShader
hands back after parsing the effect container.

The `FX-070` existence gate (`tools/graphics/mojoshader_effect_probe.cpp`) confirms the shape
concretely. MojoShader hands a backend the **raw token buffer**: `MOJOSHADER_compileShaderFunc`
takes `(tokenbuf, bufsize, ...)`, so a Direct3D 9 backend's implementation of it is
`CreateVertexShader` / `CreatePixelShader` on that buffer, with nothing translated.

Two corrections to the obvious reading, both found by building the probe rather than reading
headers:

- **The parser does not run without a backend.** `MOJOSHADER_compileEffect` refuses a null context
  outright and calls `compileShader` for every shader object in the container, so "parse the
  container only" is not an available mode. The backend is small, but it is mandatory.
- **Reflection has to come from a profile.** The backend's `getParseData` must return a
  `MOJOSHADER_parseData`, which is produced by `MOJOSHADER_parse` -- and the profile that returns
  reflection *without* translating, `BYTECODE`, is one of the four the pin disables. So `FX-070`
  either turns `SUPPORT_PROFILE_BYTECODE` back on for its configuration, or parses through a
  translating profile and discards the output. That decision belongs to the task, and it is a build
  switch either way rather than a design problem.

This is still a smaller job than any other backend here and should be scheduled ahead of the harder
ones. The caveat is platform, not design: the renderer needs a live Direct3D 9 device, so it can
only be gated on a Windows or DXVK-native configuration, and this project develops on Linux. The
implementation is portable to write and cannot be verified here.

#### Assessed feasible, not yet scheduled

Each of these can consume a MojoShader output, but none has an adapter and each carries an open
question that a prototype has to answer before a task is worth writing.

| Renderer | What it consumes today | Open question |
|---|---|---|
| LLGL | GLSL compiled through glslang | LLGL's own reflection and binding model has to be reconciled with MojoShader's register-based uniform layout |
| Diligent | `ShaderCreateInfo` with HLSL or GLSL source, converted internally | Which source language to feed given Diligent picks its native API at runtime -- the choice may have to vary per device |
| Magnum | GLSL through `GL::Shader` at `Version::GL330` | Whether MojoShader's GLSL output satisfies core-profile 330 without the compatibility constructs its older dialects rely on |
| Sokol | Backend-specific shader source or bytecode, normally produced offline by `sokol-shdc` | Sokol's uniform-block model is fixed at shader-creation time and does not obviously accommodate a register file assigned per pass |
| WickedEngine | HLSL compiled to SPIR-V or DXIL | The HLSL profile is Windows/DXVK-gated in the pin, so a non-Windows route would need the pin's own build switches changed |
| DirectX 12 | HLSL compiled at runtime through `D3DCompile` | MojoShader's HLSL profile targets a Shader Model 3 era dialect; whether `D3DCompile` accepts it at a model D3D12 will load is unverified |
| DirectX 10 | Same family as D3D11 but with no MojoShader adapter | Whether the D3D11 adapter can be retargeted or whether the binding layer must be rewritten |

#### Unsupported by design

These renderers cannot execute Shader Model 2/3 programs at all. The rationale is a property of the
target API, not a gap in CNA, so each keeps `SupportsCompiledEffects()` false permanently and
refuses a compiled `Effect` explicitly.

- **No programmable pipeline in the target API**: `DIRECTX1`, `DIRECTX2`, `DIRECTX3`, `DIRECTX5`,
  `DIRECTX6`, `DIRECTX7` (fixed-function Direct3D), `GLIDE`, `GDI`, `OPENGL1`, `OPENGL2`,
  `OPENGLES1`, `OPENVG`.
- **Shader model too old**: `DIRECTX8` reaches Shader Model 1.x, which cannot express the SM2/3
  programs an XNA 4.0 effect carries.
- **2D raster and document APIs with no shader stage CNA can target**: `SDL_RENDERER`,
  `DIRECT2D`, `CANVAS`, `HTML_DOM`, `SVG_DOM`, `SKIA`, `BLEND2D`, `SOFTWARE`, `FREEDIRECT`.
- **Programmable, but not through anything MojoShader emits**: `PORTABLEGL`, whose shader stage is
  a pair of C function pointers rather than a compiled program -- there is nothing for a translated
  shader to become. `WEBGPU` consumes WGSL, which the pinned MojoShader does not emit; it is
  reconsidered only if a SPIR-V route into wgpu-native is proven, and stays out of scope for v1.
- **Not rendering backends**: `HEADLESS`, `STUB`.

#### What this assessment does not claim

A "feasible" verdict is a reading of two toolchains, not a prototype. Nothing above has been built,
and the pattern from FNA3D is that the expensive part is never the translation -- it is the binding
layer, the uniform routing and the state mapping, none of which a feasibility reading exercises.
Each row becomes a task only after a spike proves its open question, and no renderer's capability
flips to true before it passes the `FX-060` shared suite in full.

### 10.4 Explicit non-goals for v1

- compiling HLSL `.fx` source at runtime;
- treating MonoGame MGFX as XNA/FNA Effect Framework bytecode;
- translating compiled effects through `CNAEXT::ShaderEffect`;
- enabling a capability after parser-only or shader-only success;
- silently substituting stock shaders, ignoring pass states, or falling back to pass 0;
- requiring every fixed-function or CPU renderer to emulate arbitrary programmable shaders.

### 10.5 Limitation classes on a supported backend (2026-08-18)

Once a renderer advertises `CompiledEffects == true`, every remaining "this does not work" needs to
say *which kind* of not-working it is, because the two kinds carry different obligations. Folding
them together is how a compiled-Effect gap gets excused as a renderer property, and how a renderer
property gets mistaken for something the FX work still owes.

**Case 1 - renderer-wide limitation.** The renderer does not support the operation through *any*
route. A compiled Effect is not expected to add it, and the general capability surface should already
say so. The correct behaviour is a refusal that names the operation and says it is renderer-wide.

**Case 2 - compiled-Effect-specific limitation.** The renderer performs the operation elsewhere, and
only the compiled-Effect path cannot. This is a debt of the FX work, it belongs in this plan with a
task ID, and the refusal must say so rather than implying the renderer cannot do it.

Neither case may ever be a silent fallback, a substituted texture, an ignored state or an ignored
pass. That is the line `CompiledEffects == true` is a promise about.

| Limitation | Class | FNA3D | SDL_GPU | EasyGL | Vulkan | Task |
|---|---|---|---|---|---|---|
| Vertex-stage texture sampling from `GraphicsDevice.VertexTextures`/`VertexSamplerStates` | **Case 1**, renderer-wide on *every* CNA renderer -- `IGraphicsRenderer` has no hook and `GraphicsDevice` pushes neither collection anywhere | n/a | n/a | n/a | n/a | `FX-109` |
| Vertex-stage sampling from an Effect's own `sampler_state` | Supported on FNA3D; **Case 1** elsewhere, since those renderers have no vertex-sampler path at all | works | refused by name | refused by name | refused by name | `FX-109` |
| A compiled sampler bound to a `TextureCube` | **Closed** -- all four sample one | works | works | works | works | `FX-110` |
| A compiled sampler bound to a `Texture3D` | **Case 1** on both that refuse. EasyGL: pinned MojoShader's GLSL ES output omits the required `sampler3D` precision qualifier, so the effect cannot be created on those profiles (CNA's own compiled route resolves and binds all three kinds). SDL_GPU: it samples a `Texture3D` in NO route, stock or compiled -- reclassified from Case 2 on 2026-08-18 after checking, the lifetime observation was right but the conclusion was not | works | refused, reason recorded | ES profiles refused, reason recorded | works | `FX-110` |
| A texture whose dimension does not match the declared sampler | **Closed** -- refused on all four; it used to sample black silently | refused at assignment | refused at draw | refused at draw | refused at draw | `FX-110` |
| A compiled Effect sampling a `RenderTarget2D` | Was **Case 2** (refused outright); now works everywhere | works | works | works, via a row-order-corrected copy | works | `FX-099` |
| `MipMapLevelOfDetailBias` on the OpenGL ES profiles | **Case 1** -- `GL_TEXTURE_LOD_BIAS` does not exist in OpenGL ES, which is why FNA3D's own GL driver skips it | works | works | desktop profiles only | works | `FX-083` |
| `AddressW` on the OpenGL ES 2 / WebGL 1 profiles | **Case 1** -- no sampler objects and no `GL_TEXTURE_WRAP_R`; those profiles have no volume textures either | works | recorded, unobservable (2D-only sampling) | ES 3+ profiles only | works | `FX-092` |
| A compiled draw after a GL context recreation | **Case 2** -- the renderer's own resources recover; MojoShader's context and its linked programs do not | n/a (no context loss) | n/a (no context loss) | refused by name | n/a (no context loss) | `FX-107` |
| More than one PER-VERTEX stream | **Case 1** wherever refused. SDL_GPU and Vulkan both report `MultiStreamVertexInput` false because their stock pipelines derive input elements from a byte stride, so `GraphicsDevice` refuses before the FX layer is reached -- for stock draws equally | works | renderer-wide, capability says so | works | renderer-wide, capability says so | REMED-GFX-201 |
| Instanced draws with a compiled Effect | **Case 1** on SDL_GPU, which has no instanced draw path at all and refuses by name. Implemented on Vulkan by `FX-112` after the shared contract's gate was corrected to stop excusing it | works | refused by name (renderer-wide) | works | works | `FX-112` |
| `SamplerState.MaxMipLevel`/`MipMapLevelOfDetailBias` on SDL_GPU's **stock** draw families | **Case 1**, and the inverse of the usual direction: the compiled route carries both, the stock families still capture only filter/addressing/anisotropy | works | stock families only | works | `docs/sampler-state-support.md` §6b |
| `EffectParameter`'s numeric accessors on an object parameter | Was **Case 1**, shared layer; **closed** -- `RequireNumericParameter` refuses them with `InvalidCastException`, as XNA does | works | works | works | `FX-105` |

## 11. Recommended critical path

The shortest dependency-safe implementation sequence is:

```text
FX-001..006
    -> FX-010..019
    -> FX-020..025
    -> FX-030..038 (FNA3D direct-constructor vertical slice)
    -> FX-040..043 (XNB path)
    -> FX-050..057 (production gate)
    -> FX-060..069 (renderer rollout)
    -> FX-080..090 (silent-fallback repair; done 2026-08-17)
```

The repair phase is at the end because that is when it happened, not because it belongs there.
`FX-084`-`FX-088` widened the `FX-060` suite into something a rollout can actually be gated on, and
that should have preceded `FX-061`/`FX-062` rather than followed them. **The next backend runs the
widened suite from the start**, which is the whole point of it living in `tests/support/`.

Do not begin several renderer ports before the FNA3D conformance suite and public semantics are
stable. Otherwise each backend will encode a different interpretation of arrays, pass state,
cloning, and failure behavior, making later convergence much more expensive.

## 12. Decisions to approve before implementation

This plan recommends the following decisions as one package. Reviewers should resolve any change
to them before FX-010 because they determine public ABI and backend ownership:

1. v1 accepts XNA/FNA Effect Framework bytecode, not `.fx` source or MGFX.
2. FNA behavior is the compatibility oracle and FNA3D is the first production backend.
3. Compiled effects receive a separate runtime interface and `CompiledEffects` capability.
4. The common graphics layer owns public reflection and per-instance mutable values; renderers own
   native shaders, native pass state, and device-bound resources.
5. The project uses one compatible MojoShader build, initially the revision pinned by FNA3D.
6. A backend remains explicitly unsupported until it passes the entire shared conformance suite.
7. Parser hardening, lifecycle tests, and the XNB reader are part of the FNA3D production gate,
   not follow-up polish.

## 13. Source references used by this analysis

- CNA implementation and tests at the planning baseline named at the top of this file, especially
  [`Effect.cpp`](../modules/graphics/src/Xna/Effect.cpp),
  [`ThirdPartyFNA3D.cmake`](../cmake/ThirdPartyFNA3D.cmake), and the
  [FNA3D effect fixture provenance](../modules/renderers/fna3d/effects/README.md).
- FNA's [`Effect`](https://github.com/FNA-XNA/FNA/blob/master/src/Graphics/Effect/Effect.cs) and
  [`EffectReader`](https://github.com/FNA-XNA/FNA/blob/master/src/Content/ContentReaders/EffectReader.cs)
  implementations as the compatibility oracle.
- FNA3D's [public effect API](https://github.com/FNA-XNA/FNA3D/blob/3240147/include/FNA3D.h) and its
  OpenGL, D3D11, and SDL_GPU MojoShader integrations at CNA's `3240147` pin.
- MojoShader's [public Effect Framework and profile API](https://github.com/icculus/mojoshader/blob/6333f74dbd5644789a63e903816441b16c1e8b60/mojoshader.h)
  at the FNA3D-pinned revision.
- MonoGame's [`Effect` implementation](https://github.com/MonoGame/MonoGame/blob/develop/MonoGame.Framework/Graphics/Effect/Effect.cs)
  only to establish that MGFX is a distinct format, not as the behavioral oracle for XNA/FNA
  bytecode.
