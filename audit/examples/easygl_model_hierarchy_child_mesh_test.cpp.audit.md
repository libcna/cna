# Audit: examples/easygl_model_hierarchy_child_mesh_test.cpp

## Metadata

- Source file: `examples/easygl_model_hierarchy_child_mesh_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `Model::Draw` per-mesh `ParentBone` resolution test
  (also compiled/run under `examples-tests-vulkan`, per `cmake/Tests/VulkanTests.cmake` line 805 — a shared,
  backend-parameterized test source)
- File type: C++ example/integration-test executable (`ModelHierarchyChildMeshTest : Game`, `main()`)
- Related production code: `Model.cpp` (`Draw`, the 4-arg constructor), `ModelMesh.cpp`/`.hpp`
  (`getParentBoneProperty`), `ModelBone.cpp` (`AddChild`)
- XNA/FNA relevance: `Model::Draw`'s `mesh.ParentBone.Index`-keyed bone lookup is real XNA 4.0 behavior; judged
  against `FNA/src/Graphics/Model.cs` line 121 (`effectMatricies.World = sharedDrawBoneMatrices[mesh.ParentBone.Index]
  * world;`)
- Main related tests: this file (Task 439); depends on the 4-arg `Model` constructor added specifically to make
  this test possible (see the file's own header comment and Purpose below)

## Purpose

Directly tests the specific claim in `Model::Draw` (`Model.cpp` line 125-126) that each `ModelMesh`'s **own**
`ParentBone` — not always bone 0 — is used to select which absolute bone transform is composed with `world`. Builds
a root bone (identity) and a child bone (`Translate(0.6, 0, 0)`) via `ModelBone::AddChild`, two small non-overlapping
red/blue quads (`meshRoot`, `meshChild`), and constructs the `Model` via the 4-argument constructor with
`{ nullptr, &child }` as the per-mesh parent-bone vector — `meshRoot`'s `ParentBone` stays null (defaults to bone 0),
`meshChild`'s is explicitly the child bone. If `Model::Draw` resolves per-mesh bones correctly, `meshChild` renders
shifted +0.6 NDC units right of `meshRoot`; if the code regressed to "always bone 0," both would render at the same
(root) position. Correct placement per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — this is a genuine, non-trivial regression test: the header comment's own account of a real historical
gap (`ModelMesh::parentBone_` never assigned via any public path) was cross-checked against `Model.cpp`'s actual
4-arg constructor and matches; the pixel-sampling logic correctly derives screen coordinates from the same NDC
transform the bone translation operates in, so a "still always bone 0" regression would genuinely fail this test
rather than accidentally pass.

## Checklist Results

### API / XNA / FNA parity
`Model(GraphicsDevice*, std::vector<ModelBone*>, std::vector<ModelMesh*>, std::vector<ModelBone*> meshParentBones,
std::size_t rootBoneIndex = 0)` — the 4-arg (5-parameter) constructor used at line 145 — is `NOXNA`-marked
(`Model.hpp` lines 52-56), correctly, since FNA has no public equivalent (FNA's `ModelReader` wires `ParentBone`
internally via reflection/direct field access unavailable to C++). Verified the constructor's actual behavior
(`Model.cpp` lines 25-48): the 3-arg delegate runs first (sets `root_` to `bones[0]` if non-empty), then
`rootBoneIndex` is validated/applied, then (only if `meshParentBones` is non-empty) each mesh's `parentBone_` is set
directly — matches this test's usage (`{ nullptr, &child }`, exactly 2 entries for 2 meshes) with no
out-of-range/empty-vector edge case hit here (that edge case is presumably covered by `ModelTests.cpp`'s own unit
tests per the file's own comment, not duplicated here).

### Behavioral correctness
Traced `Model::Draw` (`Model.cpp` lines 103-134) against this exact fixture: for `meshRoot`,
`getParentBoneProperty()` is `nullptr` → `boneIdx=0` → `sharedDrawBoneMatrices_[0]` = root bone's absolute transform
= identity (root has no parent, `CopyAbsoluteBoneTransformsTo` line 69-72) → `meshRoot`'s effect gets
`World = Identity * Identity = Identity`, rendering at its own local origin (screen center). For `meshChild`,
`getParentBoneProperty()` returns `&child` → `boneIdx = child.getIndexProperty() = 1` →
`sharedDrawBoneMatrices_[1]` = `child->getTransformProperty() * dest[parentIdx=0]` (line 76-78) =
`Translate(0.6,0,0) * Identity = Translate(0.6,0,0)` → `meshChild` renders shifted +0.6 on X. This is exactly what
the test's two `check()` assertions (lines 162-165) verify by sampling NDC `(0,0)` and `(0.6,0)` respectively — a
faithful, correctly-derived expected outcome, not a guessed tolerance.

### Logic
`colourMatch()` (lines 55-60) uses a per-channel `±40` tolerance — loose enough to tolerate any blend/AA edge
bleed at the sampled single-pixel point but tight enough that Red (255,0,0) vs Blue (0,0,255) vs Green(clear,
0,255,0) remain unambiguous given the channel deltas involved (each pair differs by ≥215 in at least one channel
that matters) — a sound choice, not an arbitrarily-loose check that would pass regardless of correctness. The
`sample()` lambda's NDC→pixel conversion (`(ndcX+1)*0.5*W`, `(1-ndcY)*0.5*H`, lines 150-157) is the standard
NDC-to-screen-space mapping (Y-flip included) and is applied consistently for both the root and shifted-child
samples.

### Memory/resource lifetime
`vbRoot`/`vbChild` are locals (`std::unique_ptr<VertexBuffer>`, returned by value from the `MakeQuad` helper —
correctly relies on guaranteed copy elision/move, no dangling); `ib_` is a `Game`-member reused for both meshes
(shared 6-index quad topology, safe since both parts reference it read-only during `Draw`); `root`/`child`
(`ModelBone`), `partRoot`/`partChild` (`ModelMeshPart`), `meshRoot`/`meshChild` (`ModelMesh`), `fxRoot`/`fxChild`
(`BasicEffect`) are all `Draw()`-local stack objects referenced only by non-owning pointers inside the
locally-constructed `Model` — all outlive every use within the same call, no dangling-pointer risk.

### C++ correctness
No casts/UB. Uses the **named** 3-arg `ModelMesh(GraphicsDevice*, std::string, std::vector<ModelMeshPart*>)`
constructor (`"Root"`/`"Child"`, lines 135, 139) — complementary to `easygl_model_draw_test.cpp`'s use of the
unnamed 2-arg overload, giving both `ModelMesh` constructors real coverage across the shard (consistent with the
Cross-File Observation already made for that file).

### Performance
N/A — 2 draw calls, test-only code.

### Thread safety
N/A — single-threaded, matches shard convention.

### Architecture
Correctly placed; hand-builds every object through public/`NOXNA` constructors, exercises the real
`EasyGLGraphicsBackend` via `GraphicsDevice`.

### Maintainability
179 lines; the header comment (lines 1-23) is unusually informative — it documents not just what's tested but
*why the test was previously impossible* (the pre-Task-439 `parentBone_` gap) and points to the isolated unit-test
coverage for the constructor itself (`ModelTests.cpp`) rather than duplicating it here — good separation of
concerns between this pixel-level integration test and that file's structural unit tests.

### Portability
N/A.

### Robustness
N/A — no external input.

### Testing
This is itself a test file. See Missing or Weak Tests below.

### Cross-file consistency
Matches FNA's `Model.cs Draw()` `mesh.ParentBone.Index` usage exactly, modulo CNA's necessary null-fallback (FNA's
`ModelReader` never leaves `ParentBone` null, so FNA never needs this fallback — a CNA-specific, documented
extension, not a divergence in the shared code path this test actually exercises).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Test doesn't independently verify the *content-loaded* path uses the same per-mesh-bone resolution

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / cross-file
- Location/symbol: whole file (hand-built `Model`, not `Content.Load<Model>()`)
- Evidence: this test constructs its 2-mesh model entirely by hand via the 4-arg constructor; the real
  `ModelTypeReader::Read()` (`ContentManager.cpp` lines 2394-2411) uses the *different* public API
  `mesh->setParentBoneProperty(meshBone.get())` directly on each `ModelMesh`, not the `Model` 4-arg constructor
  tested here — both paths converge on the same `parentBone_` field, but this file alone doesn't prove the JSON
  content-loader path also produces a *non-identity, spatially-verifiable* per-mesh bone (its own dedicated
  `easygl_model_json_reader_bone_hierarchy_test.cpp`, audited separately in this batch, is a purely structural test
  with no non-identity transform or pixel check).
- Why it matters: low impact — the two write paths (constructor field-assignment vs. direct setter) are both
  trivial one-line field writes with identical semantics, and `Model::Draw`'s read side (the actual logic under
  test here) is common to both, so this is a coverage-completeness note rather than a live risk.
- Suggested future action: none required; flagged for awareness only, in case a future change diverges the two
  write paths' semantics.

## Cross-File Observations

- Shares the exact `CullNone`/"Task 896 finding" pattern with every other file in this shard.
- Complements `easygl_model_draw_test.cpp` (unnamed `ModelMesh` ctor + identity-only bone) and
  `easygl_model_json_reader_bone_hierarchy_test.cpp` (structural-only, no pixel check) — together the three files
  give reasonably complete coverage of `ModelMesh`'s constructors and `Model::Draw`'s per-mesh bone resolution, with
  no single file duplicating another's actual assertions.

## Missing or Weak Tests

- No test in this file (or apparently elsewhere in this shard) exercises a **3+ mesh** model where some meshes
  share the same non-root `ParentBone` — the current fixture only ever has a 1:1 mesh:bone mapping, so a bug that
  only manifests when two different meshes resolve to the *same* non-zero bone index would not be caught here.

## Positive Findings

- The pixel-sampling math (`sample()` lambda) and the expected-shift derivation were independently re-derived from
  `Model::Draw`/`ModelBone::AddChild`/`Model`'s 4-arg constructor in this audit and match exactly — this is a real,
  discriminating regression test, not a "renders something, exits 0" placeholder.
- Good hygiene: explicitly notes where the *isolated* (non-GPU) constructor-argument-validation tests live
  (`ModelTests.cpp`) rather than re-testing constructor edge cases (empty vector, size mismatch) in a pixel test
  where they'd be harder to diagnose.

## Final Assessment

A precise, well-targeted regression test for a specific, previously-real defect (`ParentBone` never assignable).
The expected pixel outcome was independently re-derived from `Model::Draw`/`ModelBone`/`Model`'s 4-arg constructor
during this audit and matches exactly component-for-component; the only gap is that the file doesn't cross-check
the JSON content-loader's separate `setParentBoneProperty()` write path against a non-identity transform, which is
a minor coverage note, not a defect.
