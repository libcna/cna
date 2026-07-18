# Audit: include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`
- Audit status: AUDITED
- Subsystem: Graphics backend abstraction contract (shared, `backend-common` shard)
- File type: C++ header, abstract interface + supporting types
- Related header/implementation: no `.cpp` (pure interface); implemented by all 14 backend adapter classes
  (`EasyGLGraphicsBackend`, `VulkanGraphicsBackend`, `D3D9GraphicsBackend`, etc.)
- XNA/FNA relevance: none directly — this is a CNA-internal implementation-detail interface, not part of
  `Microsoft::Xna`. It plays a role loosely analogous to FNA3D's native device abstraction (`FNA3D_Device`), but
  there is no FNA source to diff against; the actual XNA-facing contract lives one layer up, in
  `Microsoft::Xna::Framework::Graphics::GraphicsDevice`/`Effect`, which call through this interface.
- Graphics backend relevance: **central** — every backend's entire capability surface is defined here.
- FNA reference: N/A (see above)
- Main related tests: exercised indirectly by every `*_test.cpp` under `examples/` that constructs a
  `GraphicsDevice` (all 14 backend shards) — no direct unit test targets this header in isolation.

## Purpose

Defines `IGraphicsBackend` and its satellite interfaces (`IVertexBufferBackend`, `IIndexBufferBackend`,
`ITextureBackend`/`ITextureCubeBackend`/`ITexture3DBackend`, `IRenderTargetBackend`/`IRenderTargetCubeBackend`,
`IEffectBackend`, `IOcclusionQueryBackend`, `ISpriteBatchBackend`) plus the `GpuDrawParams` per-draw-call payload
and `GraphicsBackendCreateArgs` construction payload. This is the single seam CNA's XNA-facing
`Graphics`/`Effect`/`SpriteBatch` classes talk through instead of hard-coding a graphics API — i.e. it *is* the
backend abstraction layer the project's own `CLAUDE.md` describes as
`include/CNA/Internal/Backends/Common/…` → `IGraphicsBackend` etc. Placement/namespace (`CNA::Internal::Backends`,
under `include/CNA/Internal/…`) is correct per the project's own layering table.

## Executive Verdict

**Mostly healthy**, with one real architecture/robustness pattern worth flagging deliberately (silent
capability-degradation defaults, below) and one moderate, evidence-based performance concern
(`GpuDrawParams` per-call zero-init cost). Both are pre-existing, clearly-considered trade-offs (the code comments
show the authors were aware of the incremental-backend-rollout cost), not oversights — but they are exactly the
kind of "documented but still real" risk this audit is meant to surface.

## Checklist Results

### API / XNA / FNA parity
N/A — not an XNA-namespace type (see Metadata).

### Behavioral correctness
The interface is deliberately staged: most state-mutating methods (`ApplyBlendState`, `ApplyDepthStencilState`,
`ApplyRasterizerState`, `ApplySamplerState`, `SetScissorRect`, `SetViewport`, `SetSwapInterval`, …) default to a
**silent no-op** rather than pure-virtual or a loud failure, explicitly to let new/partial backends "still work"
(e.g. line 793's doc comment on `DrawPrimitivesEx`). This is intentional and documented at nearly every call site,
but it means: a backend that simply forgets to override one of these has no compiler error, no runtime exception,
and no `SupportsCapability()` signal (which defaults to `true` for everything, line 850-853) — the only way the gap
is ever caught is a test that compares actual rendered output (a "golden image" test) against expectation. See
Finding F1.

### Logic
`SetRenderTargetCubeFace`/`SetRenderTargets` (lines 608-620) have sensible default implementations built from the
required primitives (`BindAsRenderTargetFace`, `SetRenderTarget2D`) — a backend that only implements the basics
still behaves reasonably for MRT/cube-face calls. `DrawInstancedPrimitivesEx`'s default throws immediately
(line 823-837) rather than silently falling back like `DrawPrimitivesEx` does — a deliberate, reasonable choice
since there's no meaningful non-instanced fallback that preserves the caller's intent, but it constructs its own
`std::runtime_error` inline instead of reusing the sibling `NotYetImplemented()` helper from
`NotYetImplemented.hpp` (this header doesn't include that one at all) — see Finding F3.

### Memory/resource lifetime
Every resource-creating factory method (`CreateTexture`, `CreateVertexBuffer`, `CreateEffectBackend`,
`CreateRenderTarget2D`, …) returns `std::unique_ptr<T>` — clean, unambiguous ownership transfer to the caller, no
raw-owning-pointer patterns in the interface itself. `GpuDrawParams` and `GraphicsBackendCreateArgs` are plain
value/aggregate types with no ownership concerns of their own (raw non-owning pointers like `texture0`,
`instanceVb`, `customEffectBackend` are documented as "valid for the duration of the call" only, which is the
correct non-owning contract for a per-call parameter struct).

The static `windowRegistry()` (lines 878-898, a function-local-static `std::unordered_map<SDL_Window*,
IGraphicsBackend*>`) stores **non-owning, raw** backend pointers keyed by window, with manual
register/unregister lifecycle. Verified all four backends that call `RegisterForWindow` (`Canvas`, `EasyGL`,
`SdlGpu`, `WebGPU`) also call `UnregisterForWindow` in their destructor/shutdown path (grep confirmed one
register+one unregister call site each). Correct today, but the contract is enforced only by convention — a new
backend that registers but forgets to unregister (or unregisters conditionally on a code path that can be skipped)
would leave a dangling pointer that `SdlInputBridge.cpp:524` / `Mouse.cpp:48` would then dereference via
`GetForWindow`. Worth a one-line check whenever a 15th backend is added, and worth noting for the per-backend
audits already in the shard queue (D3D9/D3D11/D3D12/Vulkan/Bgfx/SdlRenderer/Ascii/Software/Headless/Dx3 do not
call these at all, i.e. they don't support `TransformWindowToLogical`/`TransformLogicalToWindow` — consistent with
their default `return false` inherited behavior).

### C++ correctness
Interface classes correctly declare `virtual ~X() = default` (or an implicit one via no other special members) —
checked `IVertexBufferBackend`, `IIndexBufferBackend`, `IOcclusionQueryBackend`, `ITextureCubeBackend`,
`ITexture3DBackend`, `ITextureBackend`, `IEffectBackend`, `ISpriteBatchBackend`, `IGraphicsBackend` all have an
explicit virtual destructor — correct given every one of them is used polymorphically through
`std::unique_ptr<Base>`. `IRenderTargetBackend : public ITextureBackend` and `IRenderTargetCubeBackend : public
ITextureCubeBackend` inherit the base's virtual destructor, also correct. No slicing risk found (nothing is passed
or stored by value where a derived type would be expected). `[[nodiscard]]` is used consistently on getters whose
return value cannot be sensibly ignored (`GetVertexCount`, `GetIndexCount`, `IsThirtyTwoBit`, `SupportsCapability`,
`SupportsDepthStencil`, `GetMultiSampleCount`, …).

### Performance
**F2 (see Detailed Findings)**: `GpuDrawParams` (lines 363-495) is a ~4.9 KB value type (dominated by
`boneTransforms[72*16]` = 4608 bytes) with in-class member initializers (`= {...}`/`= {}`) on every field, meaning
its implicit default constructor zero/default-initializes the *entire* struct — including the 1152-float bone
array — on every construction, regardless of whether the draw is skinned. The interface itself takes it by `const
GpuDrawParams&` in every `*Ex` method (good — no per-call copy at the interface boundary), but the cost is paid
wherever the *caller* constructs one, which per the class's own doc comment (line 358-361, "Populated via
`Effect::FillGpuDrawParams()` before each draw call") sounds like it happens fresh per draw call rather than being
reused. This needs corroboration against `Effect::FillGpuDrawParams()`'s actual call pattern (queued for the
`xna-graphics` shard audit — flagged here so it isn't lost) before elevating past `MEDIUM`/`LOW confidence`.

`IEffectBackend`'s uniform setters (`SetUniformFloat` etc., lines 279-295) take `const char* name` — a
name-based uniform lookup on every draw call is a classic hot-path cost (e.g. `glGetUniformLocation` if not
cached) but whether it's actually a problem is entirely backend-implementation-dependent; deferred to each
backend's own audit rather than judged from the interface alone.

### Thread safety
`windowRegistry()`'s function-local static (lines 894-897) is a "magic static" — its *initialization* is
thread-safe per C++11, but concurrent `RegisterForWindow`/`UnregisterForWindow`/`GetForWindow` calls from
different threads would race on the underlying `std::unordered_map` (insert/erase/find with no external
synchronization is undefined behavior for `std::unordered_map`). Traced every call site (see Memory/resource
lifetime above): all registration happens in backend constructors/destructors, all lookups happen from
`SdlInputBridge`/`Mouse` — both are normal SDL-event/main-thread operations in CNA's single-threaded game-loop
model, so this is **not a live bug today**, but the map has zero defense if a future caller (e.g. an async
resource-loading thread) ever touches it. `LOW` severity, `MEDIUM` confidence (no reproducing caller sequence
found; flagged as a latent risk, not a confirmed defect).

### Architecture
Clean single-seam abstraction: this is the only header every backend adapter must implement in full, and the
"required vs. optional" split is expressed structurally (pure virtual vs. defaulted virtual) rather than through a
separate capability-descriptor type for most features — except `SupportsCapability(CNA::GraphicsCapability)`
(line 850), which *is* a real, queryable capability-negotiation mechanism, just not applied consistently to every
optional method (see F1). The header pulls in `Microsoft::Xna::Framework::*` types (`Color`, `Rectangle`,
`Vector2`, `Matrix`, `SpriteEffects`, `PrimitiveType`, `SetDataOptions`, `VertexElement`) purely as vocabulary
types via `using` aliases (line 30-39) — a one-directional dependency (backend layer depends on public XNA value
types, never the reverse), which matches the project's own layering table and is the correct direction.

### Maintainability
1024 lines for a single header is large but proportionate to its role — it is the entire backend contract, and
the bulk of the length is Doxygen documentation (a real strength here, not padding — see Positive Findings) plus
`GpuDrawParams`'s long, individually-commented field list. No dead code, no `TODO`/`FIXME` markers of the
"forgotten" kind — the two explicit `// TODO: SDL dependency should be abstracted later` comments (lines 192, 550,
961) are self-aware, tracked architectural notes about `SDL_Texture*`/`SDL_Window*`/`SDL_Renderer*` leaking into an
otherwise backend-agnostic interface, which is a legitimate, currently-accepted layering compromise (every backend
happens to run on top of SDL for windowing) rather than an oversight — worth surfacing in
`AUDIT_CROSS_CUTTING_FINDINGS.md` as an architecture note since it recurs across this file in three places.

### Portability
No platform-specific code in this header itself (that's pushed down into each backend implementation, correctly).

### Robustness
See F1 — the interface's own defaults favor "degrade silently" over "fail loudly" for most optional 3D-state/
effect-parameter methods, while a few (`ReadBackbuffer` line 560-563, `DrawInstancedPrimitivesEx` line 835-837)
throw. The split is intentional (documented per-method) but asymmetric — see F1/F3.

### Testing
No direct unit test exists for this header as an isolated unit (expected — it's a pure interface with no logic of
its own to unit-test other than default-method fallback behavior, which *is* implicitly exercised by every backend
integration test that doesn't override a given optional method).

## Detailed Findings

### F1 — Optional-capability defaults degrade silently rather than being negotiable

- Severity: MEDIUM
- Confidence: HIGH (the code itself, not just inference)
- Category: architecture / robustness
- Location/symbol: `IGraphicsBackend::ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/
  `ApplySamplerState`/`SetScissorRect`/`SetViewport` (lines 624-676), `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`
  (lines 796-820), `SupportsCapability` (lines 850-853)
- Evidence: all of the above default to either a no-op or (for the `*Ex` draw methods) a silent fallback to
  flat-colored rendering, while `SupportsCapability()` — the interface's own purpose-built capability-query
  mechanism — defaults to unconditionally `true`. There is no way for a caller to distinguish "this backend
  genuinely doesn't support skinning/dual-texture/env-mapping and rendered flat color instead" from "this backend
  supports it and flat color really was requested," short of visually comparing pixels.
- Why it matters: a backend that regresses (e.g. a refactor accidentally stops calling into its skinning path and
  falls through to the base `DrawPrimitivesEx`) produces *wrong pixels*, not a build error, a thrown exception, or
  even a log line — only a golden-image test catches it, and per the manifest not every backend×effect combination
  has one yet (to be confirmed precisely during the Pass 4 capability-matrix work).
- FNA/XNA comparison: N/A (no FNA equivalent layer).
- Related files: every backend's own audit report should note, for each state/effect method it does *not*
  override, whether that omission is an intentional documented limitation (e.g. SDL_Renderer/Dx3/Canvas being
  2D-only) or an actual gap.
- Suggested future action (not implemented by this audit): consider having `SupportsCapability()` (or a sibling
  method) become the load-bearing source of truth that backends must positively opt into per real feature, rather
  than defaulting to `true`, OR ensure the capability matrix / golden-image test coverage is complete enough that
  a silent-fallback regression is always caught in CI. Recorded for `AUDIT_GRAPHICS_BACKEND_MATRIX.md` and
  `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### F2 — `GpuDrawParams` unconditionally zero-initializes a ~4.6 KB bone-transform array on every construction

- Severity: MEDIUM (pending corroboration — see confidence)
- Confidence: MEDIUM (interface-level evidence is solid; call-frequency evidence is not yet gathered)
- Category: performance
- Location/symbol: `GpuDrawParams::boneTransforms` (line 431), whole-struct in-class initializers (lines 363-495)
- Evidence: `float boneTransforms[72 * 16] = {};` plus every other field's in-class initializer means the
  compiler-generated default constructor touches the entire ~4.9 KB struct on every instantiation. Doc comment
  (line 358-361) states it is "Populated via `Effect::FillGpuDrawParams()` before each draw call," implying
  per-draw-call construction.
- Why it matters: if a fresh `GpuDrawParams` is stack-constructed per draw call (as opposed to one instance reused
  and selectively mutated across a frame), every single non-skinned draw call pays the cost of zeroing 1152 floats
  it will never read. For a scene with thousands of draw calls (batched sprites, particle-heavy scenes) this is a
  measurable, avoidable memset-equivalent cost.
- FNA/XNA comparison: N/A (CNA-internal type).
- Related files: `src/Microsoft/Xna/Framework/Graphics/Effect.cpp` / `BasicEffect.cpp` / `SkinnedEffect.cpp` (owns
  `FillGpuDrawParams()` — queued for the `xna-graphics` shard audit to confirm actual construction frequency and
  close out this finding's confidence level one way or the other).
- Suggested future action (not implemented by this audit): if confirmed per-draw-call, consider only
  zero-initializing `boneTransforms` when `skinned == true`, or hoisting a single `GpuDrawParams` per `Effect`
  instance that's mutated in place rather than reconstructed.

### F3 — `DrawInstancedPrimitivesEx`'s default failure path duplicates `NotYetImplemented()` instead of reusing it

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / simplification
- Location/symbol: `IGraphicsBackend::DrawInstancedPrimitivesEx` default body (lines 823-837);
  `CNA::Internal::Backends::NotYetImplemented()` (`NotYetImplemented.hpp` line 26-29)
- Evidence: `NotYetImplemented.hpp`'s own header comment explains it exists specifically so backend code doesn't
  duplicate a "throw naming the unimplemented capability" helper — but `IGraphicsBackend.hpp` never includes that
  header, and hand-rolls an equivalent `throw std::runtime_error(...)` inline for its own default method instead.
  Same for `ReadBackbuffer`'s default (line 560-563).
- Why it matters: purely cosmetic/consistency — behavior is correct either way (both throw a descriptive
  `std::runtime_error`) — but it's a small missed opportunity for the exact consolidation this shared helper was
  introduced for.
- FNA/XNA comparison: N/A.
- Related files: `include/CNA/Internal/Backends/Common/NotYetImplemented.hpp`.
- Suggested future action (not implemented by this audit): route these two default bodies through
  `NotYetImplemented("IGraphicsBackend", "...")` for consistency, if this file is touched again for other reasons.

## Cross-File Observations

- The three `// TODO: SDL dependency should be abstracted later` comments (`GetNativeTexture` return type,
  `GetWindowInternal`/`GetRendererInternal`, `GraphicsBackendCreateArgs::window`) mean every single backend —
  including ones that don't use SDL's 2D renderer at all (Vulkan, D3D9/11/12, Bgfx, WebGPU) — is still required to
  produce an `SDL_Window*`/`SDL_Renderer*` pair to satisfy this interface. Worth checking during each backend's
  audit whether non-`SDL_Renderer`-based backends return a real `SDL_Renderer*` or a dummy/null one, and whether
  any caller actually dereferences it unconditionally.
- `HasRealDepthBuffer` (line 235) and `SupportsDepthStencil` (line 691) are two independent, similarly-named
  capability queries (per-render-target vs. whole-device) — worth double-checking during backend audits that
  callers use the right one in the right context, since the names are easy to confuong at a call site.

## Missing or Weak Tests

No test exercises `IGraphicsBackend`'s *default* method bodies directly (e.g. constructing a minimal test double
that doesn't override `ApplyBlendState` and confirming it's a genuine no-op, or confirming `DrawInstancedPrimitivesEx`
throws the expected message on a backend that hasn't implemented it). This would be a cheap, high-value unit test
to add given how much of this interface's behavior is defined by its *defaults*, not by any one backend.

## Positive Findings

- Documentation quality is genuinely excellent and substantive — nearly every non-trivial method's Doxygen comment
  explains *why* a default exists, which backends currently override it, and what a future backend implementer
  needs to know (e.g. `CreateRenderTarget2D`'s comment on Vulkan's shared-depth-format constraint, line 576-582;
  `weightsPerVertex`'s comment tying it to a specific ported FNA shader behavior, line 434-436). This is exactly
  the kind of comment the project's own `CLAUDE.md` documentation rules ask for, done well.
- Consistent, correct use of `[[nodiscard]]`, `virtual ~X() = default`, and `std::unique_ptr` ownership throughout.
- The incremental-backend-rollout design (defaults that let a new backend compile and pass smoke tests before every
  feature is implemented) is a reasonable, deliberate engineering trade-off for a 14-backend project, not naivety —
  the risk it trades in (F1) is clearly one the authors understood and accepted, evidenced by how consistently the
  trade-off is called out in comments rather than left implicit.

## Final Assessment

A well-designed, thoroughly-documented core abstraction whose main risk is structural rather than a coding
mistake: optional-feature gaps degrade to silently-wrong pixels rather than a detectable signal, which shifts the
entire correctness burden for "does backend X actually support feature Y" onto golden-image test coverage. That
coverage's actual completeness is exactly what Pass 4's `AUDIT_GRAPHICS_BACKEND_MATRIX.md` needs to establish.
