# Audit: examples/sdlrenderer_model_draw_throws_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_model_draw_throws_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 728, `Model::Draw` on SDL_Renderer.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_ModelDrawThrows` /
  `cna_test_sdl_model_draw_throws`, `cmake/Tests/SdlRendererTests.cmake:406-408`).
- XNA/FNA relevance: direct — `Model.Draw`/`ModelMesh.Draw`/`ModelMeshPart.Effect` chain.
- FNA reference: `Graphics/Model.cs` (`Draw()`'s `mesh.Effects` iteration, lines ~103-134),
  `Graphics/ModelMesh.cs` (`Draw()`, lines 113-138), `Graphics/ModelMeshPart.cs` (`Effect` property setter,
  lines 19-59 — auto-maintains `parent.Effects`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Model.cpp` (`Draw`, lines 103-134),
  `src/Microsoft/Xna/Framework/Graphics/ModelMesh.cpp` (`Draw`, lines 41-66),
  `src/Microsoft/Xna/Framework/Graphics/ModelMeshPart.cpp` (`setEffectProperty`, lines 27-57),
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`SetVertexBuffer` lines 514-519,
  `DrawIndexedPrimitives` lines 591-630).

## Purpose

Confirms `Model::Draw()` reaches, and correctly throws from, `GraphicsDevice::DrawIndexedPrimitives` when a mesh
part has no bound vertex buffer — the only way `Model::Draw` can meaningfully touch 3D-only backend surface on
SDL_Renderer, since `VertexBuffer`/`IndexBuffer` construction itself already throws immediately on this backend
(Tasks 720-723). Builds a minimal 2-bone hierarchy + 1 mesh + 1 `ModelMeshPart` with deliberately `nullptr`
vertex/index buffers (legal since `ModelMeshPart` takes raw pointers, no construction), asserts the exact
exception message, then confirms the device is still fully usable afterward via a `Clear`+readback.

## Executive Verdict

**Healthy** — the exact exception message, the throw's origin, and (on closer inspection) the less-obvious
`ModelMeshPart::Effect`-setter-auto-populates-`mesh.Effects` mechanism this test's construction order silently
depends on are all independently verified correct against both the CNA production code and the FNA reference.

## Checklist Results

### API / XNA / FNA parity

`ModelMeshPart(VertexBuffer*, IndexBuffer*, int numVertices, int primitiveCount, int startIndex, int
vertexOffset)` (test line 70: `ModelMeshPart part(nullptr, nullptr, 4, 2, 0, 0)`) matches the constructor
signature declared in `ModelMeshPart.hpp` lines 33-35 exactly, parameter-for-parameter. This constructor is
correctly marked `NOXNA` (FNA's real `ModelMeshPart` is populated field-by-field internally by `ModelReader`, not
via a public aggregate constructor) — an intentional, correctly-tagged CNA convenience extension, not a
mislabeled XNA API.

The test's construction order — `ModelMeshPart part(...)`, then `ModelMesh mesh(&dev, { &part })` (which sets
`part->parent_ = &mesh`, `ModelMesh.cpp:16-17`), *then* `part.setEffectProperty(&fx)` — means `setEffectProperty`
runs with `parent_` already non-null. Traced `ModelMeshPart::setEffectProperty` (`ModelMeshPart.cpp:27-57`): with
`effect_` previously `nullptr` and `parent_` set, it takes the `effect_ != nullptr && parent_ != nullptr &&
!parent_->getEffectsProperty().Contains(effect_)` branch (line 52-56) and calls
`parent_->getEffectsPropertyMutable().Add(effect_)` — meaning `fx` genuinely gets registered into
`mesh.getEffectsProperty()`, not left in a mesh-level collection that stays empty. This is a faithful, correctly
implemented port of FNA's own `ModelMeshPart.Effect` setter (`ModelMeshPart.cs` lines 19-59, including the
"remove old effect only if no sibling part still uses it" logic at lines 32-48/CNA lines 32-48) — a subtle XNA
behavior (a settable public property with side effects on the *parent* object) that would be easy to port
incorrectly (e.g. as a plain field), and this audit confirms it is not.

This matters for `Model::Draw()`'s own correctness: `Model.cpp:116-131` iterates `mesh->getEffectsProperty()` to
push `World`/`View`/`Projection` into each `IEffectMatrices`-implementing effect *before* calling `mesh->Draw()` —
matching FNA's `Model.cs` `Draw()` exactly (`foreach (Effect effect in mesh.Effects) { ... }`). Because this
test's construction order causes `fx` to actually be present in `mesh.Effects`, this loop is genuinely exercised
(effect count = 1, not 0) — the test is not silently skipping the matrix-propagation path the way it would if
`setEffectProperty` had been called *before* `ModelMesh`'s constructor set `parent_` (which would have left
`mesh.Effects` empty and this loop a no-op). This ordering dependency is easy to miss on a first read and is worth
calling out explicitly as verified, not assumed.

### Behavioral correctness

`ModelMesh::Draw()` (`ModelMesh.cpp:41-66`) skips a part when `effect == nullptr || primitiveCount <= 0`
(line 46) — this test's part has `effect=&fx` (non-null, set before `Draw()` runs) and `primitiveCount=2` (> 0),
so the part is *not* skipped, reaching `graphicsDevice_->SetVertexBuffer(part->getVertexBufferProperty())` (=
`nullptr`) at line 49. `GraphicsDevice::SetVertexBuffer(const VertexBuffer*)` (`GraphicsDevice.cpp:514-519`)
accepts `nullptr` safely (`if (vertexBuffer && vertexBuffer->getIsDisposedProperty())` — short-circuits on
`nullptr`), setting `currentVertexBuffer_ = nullptr`. The pass loop then calls
`graphicsDevice_->DrawIndexedPrimitives(...)` (`ModelMesh.cpp:56-63`), which immediately
(`GraphicsDevice.cpp:606-607`) checks `currentVertexBuffer_ == nullptr` *first* (before the index-buffer or
effect checks) and throws exactly `"GraphicsDevice::DrawIndexedPrimitives: no vertex buffer is bound."` — an
exact byte-for-byte match to this test's asserted string (line 84).
Since the vertex-buffer check fires before the effect-applied check, this test's outcome does not actually depend
on `pass.Apply()`/`currentEffect_` having been set correctly first — though as established above, the mesh's
effect propagation *is* correctly exercised regardless.

### Robustness

The post-throw functionality check (lines 87-94: `Clear` + 1x1 `GetBackBufferData` near center, asserting
G/B >= 240 for a cyan fill) is a meaningful "the device did not end up in a broken state" check — reuses the same
pattern as this shard's other throw-focused tests (`sdlrenderer_occlusionquery_throws_test.cpp`,
`sdlrenderer_multisamplecount_decision_test.cpp`).

### Testing

Covers the one throw-reachable `Model::Draw()` path possible on this backend given `VertexBuffer`/`IndexBuffer`
constructing throw immediately — correctly scoped for a 2D-only backend. Confirmed (per the file's own comment
and independently by grepping `ModelMeshTests.cpp`) that no pre-existing test in this project previously exercised
`Model::Draw()` on SDL_Renderer, so this is genuine new coverage, not a duplicate.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. No LOW findings either — this file's claims all withstood independent
line-by-line re-verification, including the subtler `ModelMeshPart::Effect`/`ModelMesh.Effects` interaction that
is easy to get backwards.

## Cross-File Observations

- `examples/easygl_model_draw_test.cpp` (cited in this file's own comment as the structural template) uses the
  *identical* construction order (`ModelMeshPart` → `ModelMesh` → `part.setEffectProperty(&fx)`) — so both files
  correctly exercise the same `mesh.Effects` auto-population path; this is a consistent, shared pattern across
  the two files rather than a coincidence unique to this one.
- `ModelMeshPart::setEffectProperty`'s "only remove the old effect if no sibling part still references it" logic
  (`ModelMeshPart.cpp:34-45`) is not exercised at all by this single-part test (there is no sibling part to
  collide with) — worth noting as a gap in the *production* `ModelMeshPart`/`ModelEffectCollection` test coverage
  more broadly (a multi-part-mesh-sharing-one-effect scenario), not something this particular throws-focused file
  needs to add.

## Missing or Weak Tests

None specific to this file's own stated purpose (verifying the throw). The multi-part shared-effect removal logic
noted above would be better covered by a dedicated `ModelMeshPart`/`ModelMesh` unit test under `tests/`, not this
backend-specific throw test.

## Positive Findings

- Exact-string exception-message assertion (not just "did it throw"), giving strong regression protection against
  message drift.
- Correctly relies on (and, per this audit, correctly exercises) a genuinely subtle piece of FNA-parity behavior
  — the `Effect` setter's side effect on the parent mesh's `Effects` collection — rather than accidentally
  bypassing it through construction-order luck.
- Confirmed genuinely new coverage (no prior SDL_Renderer `Model::Draw()` test existed) with a correctly-assessed
  "safe to add" rationale, unlike the project's own documented Task 725 Texture3D/TextureCube situation which it
  explicitly contrasts itself against.

## Final Assessment

A precise, well-targeted test. Its exact-message assertion and its (correctly relied-upon) exercise of the
`ModelMeshPart`/`ModelMesh` effect-collection XNA-parity behavior both held up under independent line-by-line
verification against the production code and the FNA reference.
