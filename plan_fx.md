# Compiled XNA Effect Bytecode Support Plan

- Status: **In progress — FNA3D vertical slice implemented, conformance-suited and parser-hardened
  on `feature/fx`; `FX-057` deliberately not declared (see section 10.1's assessment)**
- Planning baseline: `develop` at `a749fdce34a5825eb80a778b5db68e11da9358f8`
- Target branch: `feature/fx`
- Scope of this document: architecture, implementation checklist, and current delivery status

## Implementation snapshot (2026-08-14)

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
- twenty-two upstream crash classes found by that campaign and fixed in the managed MojoShader patch --
  a dereferenced NULL preshader parse, a SPIR-V attribute fixup assert, register copies sized by
  the constant table rather than the parsed storage, an unbounded preshader operand count, two
  asserts on untrusted preshader tokens, an allocation sized before its own bounds check, and an
  unchecked shader-array selector index, a preshader interpreter whose every index was guarded
  only by asserts, register copies unbounded against the register file they write into, and a
  pass's shader object index used without a range or type check on a union -- with the remaining
  exposure recorded and reproducible rather than hidden (`FX-050`, `FX-051`);
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

- an independent normalized FNA oracle (`FX-005`) and the compiler-produced fixture it would be
  compared against (`FX-004`). This is **blocked by the environment, not by effort**: the FNA
  reference tool needs `mono`/`xbuild` and a built `FNA.dll`, and neither `mono` nor `dotnet`
  exists on the development machine. The synthetic fixture now covers reflection, pass identity,
  every render-state token, samplers, textures and a real Shader Model 2.0 program without a
  proprietary compiler, but comparing CNA against itself is self-consistency, not ground truth;
- a fuzz gate that runs dry on every driver (`FX-051`). Eighteen crash classes are fixed in the
  managed MojoShader patch and the campaign is clean on FNA3D's OpenGL driver; the SDL_GPU
  driver's SPIR-V emitter still asserts on hostile shader bytecode;
- `SamplerState.AddressW` reaching any renderer at all (`FX-026`), a pre-existing shared-layer gap
  that compiled sampler coverage exposed;
- additional renderer implementations (`FX-061`–`FX-069`), including EasyGL/OpenGL/OpenGL ES
  (`FX-062`) and Vulkan (`FX-064`–`FX-065`). The shared contract they must pass now exists
  (`FX-060`); until a backend passes it, its correct behavior is an explicit
  `NotSupportedException`, never a silent stock-shader fallback.

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
| FX-004 | Create a purpose-built conformance `.fx` source and committed `.fxb` covering techniques, passes, defaults, arrays, structures, annotations, textures, samplers, and states | FX-002 | Source, compiler identity/version/hash, reproduction instructions, and legal provenance are committed |
| FX-005 | Extend the FNA reference tool to emit normalized JSON reflection and deterministic pixels/state observations for all fixtures | FX-003, FX-004 | Oracle output is reproducible and checked into test data |
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
| FX-016 | Implement bounded parameter backing storage, element/member views, defaults, strings, textures, and dirty revisions | FX-002, FX-011 | FNA oracle matches for scalar/vector/matrix/array/struct getters and setters |
| FX-017 | Populate annotations and sampler-to-texture bindings from descriptors | FX-016 | Annotation lookup/defaults and texture sampler associations match the oracle |
| FX-018 | Implement compiled constructor validation, reflection population, first-technique selection, and differentiated errors | FX-006, FX-012, FX-014, FX-015, FX-016, FX-017 | Valid fixtures construct; unsupported renderer and malformed/foreign formats fail distinctly |
| FX-019 | Implement independent compiled-effect clone and lifecycle/device guards | FX-018 | Values, textures, technique, owners, disposal, and device-loss tests pass without aliasing |

### Phase C - Pass and GraphicsDevice semantics

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-020 | Implement exact compiled `EffectPass::Apply` ordering and define compiled `Effect::Apply` as current-technique pass 0 | FX-015, FX-018 | Hook/order tests and multi-pass runtime mock tests match FNA |
| FX-021 | Synchronize only dirty parameters/textures before native pass application | FX-016, FX-020 | Mock counters prove correct first upload, no redundant upload, and subview invalidation |
| FX-022 | Implement complete neutral-to-CNA blend/depth/stencil/rasterizer state translation | FX-002, FX-020 | Table-driven tests cover every supported legacy render-state token and unknown-token policy |
| FX-023 | Implement texture/sampler state translation without mutating shared immutable state objects | FX-017, FX-020 | **Done.** Per-slot tests cover every filter-component combination, addressing, LOD bias, mip level, anisotropy, exact register targeting, reflected texture binding, and clone isolation |
| FX-026 | Carry `SamplerState.AddressW` through the renderer-neutral sampler contract | FX-023 | `IGraphicsRenderer::ApplySamplerState` takes only U and V, so FNA3D mirrors U into W and a draw overwrites an effect's assigned `ADDRESSW`. Pre-existing shared-layer gap, observable only for volume textures; the value is already translated and published on the device |
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
| FX-051 | Build a libFuzzer/AFL-compatible constructor/reflection/clone harness with the fixture corpus | FX-050 | Harness, corpus and a deterministic mutation campaign **done** (`tools/graphics/compiled_effect_fuzzer.cpp`, `docs/fx-bytecode-fuzzing.md`). The campaign found twenty-two crash classes, all in pinned MojoShader and none in CNA, now fixed by the managed patch. **Clean on FNA3D's OpenGL driver** across three independent seeds (10,000/10,000/12,000 iterations) and on the SDL_GPU driver for 3,000. Still open: the SPIR-V emitter validates with `assert()` in about fifty places, so more will surface as the campaign runs longer, and an 80,000-iteration soak reached a use-after-free at ~27,900 where MojoShader publishes a destroyed effect's sampler array as the current pass's state changes |
| FX-052 | Run ASan/UBSan and renderer teardown/reset stress suites | FX-038, FX-050 | **Done for CNA-owned code.** 340 FX/Effect/XNB/capability tests pass under ASan+UBSan with zero address findings and zero CNA undefined-behaviour findings; the FX-038 reset and repeated create/apply/dispose stress cases run inside that suite. LeakSanitizer runs after all (the earlier ptrace claim was wrong) and attributes every leak record to pinned MojoShader's SPIR-V emitter or to `FNA3D_CreateDevice`, none to CNA. The remaining third-party UBSan/leak findings are recorded upstream findings, not a CNA gate |
| FX-053 | Benchmark construction, clone, dirty uploads, and draw overhead; add immutable artifact cache only if justified | FX-037 | **Done.** `tools/graphics/compiled_effect_benchmark.cpp` plus the baseline table in `docs/fx-compiled-effects.md`. Decision: **no cache**. Construction cost tracks embedded shader work rather than file size, `Clone()` is ~7.5x cheaper than constructing the same effect because the native clone reuses translated artifacts, dirty tracking keeps a no-change apply at ~2.9 us, and a compiled pass draws no slower than a stock effect. A bytecode-keyed cache would add cross-instance sharing risk for a case `Clone()` already covers |
| FX-054 | Run full stock-effect, `ShaderEffect`, SpriteBatch, model, primitive, and renderer regression suites | FX-037, FX-043, FX-052 | **Done.** The whole `CnaTests` binary runs under FNA3D: 5,997 pass and every remaining failure is explained -- one real regression from this branch (a stale FNA3D instancing message) fixed here, three `MouseCursorTest` failures caused by `SDL_VIDEODRIVER=offscreen` having no system cursors, one render-target readback that fails only on the SDL_GPU/Vulkan driver and passes on FNA3D's OpenGL driver, and one pre-existing FNA3D device-lifetime crash unrelated to compiled effects, now recorded in `known_bugs.md` |
| FX-055 | Publish FNA3D support documentation, format/error guide, capability matrix, dependency notices, and migration examples | FX-043, FX-054 | **Done.** `docs/fx-compiled-effects.md` covers the format boundary, loading, reflection, values, techniques/passes, published pass state, samplers, clone, lifetime, the renderer matrix, an error table, XNA-to-CNA migration, and the dependency/licence notices |
| FX-056 | Update/supersede the old FX plan documents and Phase 74 rows without erasing their historical record | FX-055 | **Done.** `docs/fx-bytecode-support-plan.md` and `docs/shader-effect-vs-fx-bytecode.md` carry supersession banners and point at the current guide; `plan_graphics.md` Phase 74 keeps its original rows and adds a row-by-row disposition (obsolete / delivered / re-scoped / carried forward) so none of them can be picked up again |
| FX-057 | Declare the FNA3D vertical slice usable | FX-051, FX-052, FX-054, FX-055, FX-056 | **Blocked, deliberately.** Six of eight exit criteria pass; the oracle criterion needs `FX-005` (no `mono`/`dotnet` on the development machine) and the safe-failure criterion needs `FX-051` to run dry. Assessed row by row in section 10.1 |

### Phase G - Additional renderer waves

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-060 | Extract a reusable shared backend conformance suite from the FNA3D tests | FX-057 | **Done.** `tests/support/CNA/TestSupport/` holds the Direct3D 9 Effect Framework format constants, the deterministic fixture builders (including the hand-assembled Shader Model 2.0 program) and nine contract sections -- format, reflection, techniques/passes, render state, state policy, samplers, texture binding, clone and lifecycle -- plus an explicit unsupported-backend contract. A backend adds one test that builds its device and calls `RunCompiledEffectContract`. FNA3D runs it and static-asserts the neutral constants against its parser's own enumerations |
| FX-061 | Implement and gate SDL_GPU through the MojoShader SDL adapter | FX-030, FX-060 | Full shared suite and native lifecycle tests pass before capability becomes true |
| FX-062 | Implement and gate EasyGL/OpenGL-family support through MojoShader GL | FX-030, FX-060 | Full shared suite passes and GL state caches remain coherent |
| FX-063 | Implement and gate DirectX 11 through the MojoShader D3D11 adapter | FX-030, FX-060 | Full shared suite passes on the supported Windows CI matrix |
| FX-064 | Prototype direct MojoShader SPIR-V generation/linking for Vulkan | FX-030, FX-060 | Reflection, uniform binding, vertex linkage, and one multi-pass fixture work without glslang |
| FX-065 | Complete and gate Vulkan after the prototype | FX-064 | Full shared suite and Vulkan validation layers pass |
| FX-066 | Prototype and gate Metal support if the pinned profile meets CNA requirements | FX-030, FX-060 | Decision record plus full suite before enabling capability |
| FX-067 | Assess DirectX 9, D3D12, LLGL, Diligent, Magnum, Sokol, Wicked, and other programmable renderers individually | FX-060 | Each backend gets an accepted implementation plan or documented unsupported rationale |
| FX-068 | Keep bgfx false until a reproducible bgfx-native shader packaging route is proven | FX-060 | Feasibility record covers shaderc format, reflection, pass states, and redistribution |
| FX-069 | Publish the final cross-renderer support matrix and project-wide completion definition | FX-061, FX-062, FX-063, FX-065, FX-066, FX-067, FX-068 | Every renderer is tested-supported or intentionally unsupported; no silent fallback exists |

### Phase H - Optional future formats and tooling

| ID | Task | Depends on | Acceptance criteria |
|---|---|---|---|
| FX-070 | Evaluate a separate MGFX/mgfxo reader and runtime without weakening FX bytecode diagnostics | FX-057 | Written compatibility/design decision; no format guessing |
| FX-071 | Evaluate an offline CNA effect compiler/package tool | FX-069 | Reproducible cross-platform artifact, reflection, and licensing design |
| FX-072 | Evaluate runtime `.fx` source compilation only if a concrete game-port requirement justifies it | FX-069 | Security, compiler redistribution, caching, and platform availability are resolved |

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

#### Assessment (2026-08-14)

`FX-057` is **not** declared. Six of the eight criteria pass; two do not, and neither is a matter
of remaining effort alone.

| Criterion | State |
|---|---|
| byte-array and XNB loading | **Pass** — both paths tested, including an end-to-end ContentManager load and draw |
| reflection matches the FNA oracle | **Not met.** There is no oracle. `FX-005` needs the FNA reference tool, which needs `mono`/`xbuild` and a built `FNA.dll`; neither `mono` nor `dotnet` exists on this machine. Reflection is currently verified against the format and against CNA's own fixtures, which is self-consistency, not ground truth |
| parameters, annotations, textures/samplers, techniques, passes, states | **Pass** — covered by the shared conformance suite and the FNA3D-specific tests |
| SpriteBatch and 3D pixel tests | **Pass** — deterministic pixels for both paths and for a blend-factor state oracle |
| clone and lifetime | **Pass** — clone chains, device reset, disposal ordering, repeated cycles |
| malformed input and unsupported renderers fail explicitly and safely | **Partial.** Unsupported renderers refuse by name, and CNA's own layer rejects every malformed category tested. But a fuzz campaign still reaches a wild read inside pinned MojoShader after a few thousand mutations, so "safely" does not yet hold for arbitrary hostile content. Documented as a trust boundary rather than claimed |
| fuzz/sanitizer and regression gates clean | **Partial.** Sanitizers are clean for CNA-owned code (`FX-052`) and the full regression is explained (`FX-054`), but the fuzz gate is not clean (`FX-051`) |
| `CompiledEffects` true only on FNA3D, documented precisely | **Pass** |

So the accurate public statement today is: compiled effects work on FNA3D for content the game
ships, are covered by a portable conformance suite, and are documented -- but the feature is not
yet declared usable, because it has no independent oracle and cannot yet promise safe failure on
hostile input.

### 10.2 Project-wide completion

The project-wide gap is closed only after every renderer has either passed the shared conformance
suite and enabled `CompiledEffects`, or has an explicit documented unsupported rationale aligned
with that renderer's purpose. FNA3D support alone is a valuable production milestone, but must not
be presented as universal CNA renderer support.

### 10.3 Explicit non-goals for v1

- compiling HLSL `.fx` source at runtime;
- treating MonoGame MGFX as XNA/FNA Effect Framework bytecode;
- translating compiled effects through `CNAEXT::ShaderEffect`;
- enabling a capability after parser-only or shader-only success;
- silently substituting stock shaders, ignoring pass states, or falling back to pass 0;
- requiring every fixed-function or CPU renderer to emulate arbitrary programmable shaders.

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
```

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
  [`Effect.cpp`](modules/graphics/src/Xna/Effect.cpp),
  [`ThirdPartyFNA3D.cmake`](cmake/ThirdPartyFNA3D.cmake), and the
  [FNA3D effect fixture provenance](modules/renderers/fna3d/effects/README.md).
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
