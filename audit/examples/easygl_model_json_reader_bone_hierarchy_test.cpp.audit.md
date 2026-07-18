# Audit: examples/easygl_model_json_reader_bone_hierarchy_test.cpp

## Metadata

- Source file: `examples/easygl_model_json_reader_bone_hierarchy_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `.model.json`/`.cnj` `ModelTypeReader` per-mesh bone
  regression test (also compiled/run under `examples-tests-vulkan`, `cmake/Tests/VulkanTests.cmake` line 840)
- File type: C++ example/integration-test executable (`ModelJsonReaderBoneHierarchyTest : Game`, `main()`); no
  rendering — a pure structural test, `Draw()` is an empty override
- Related production code: `ModelTypeReader::Read()` (`ContentManager.cpp`, per-mesh bone creation at lines
  2398-2411), `Model`'s 3-arg constructor (`Model.cpp` lines 15-23), `ModelBoneCollection::operator[](const
  std::string&)` (`ModelBoneCollection.cpp` lines 13-19)
- XNA/FNA relevance: `Model.Bones`, `ModelBone.Parent`, `ModelMesh.ParentBone` are real XNA 4.0 members; the
  `.model.json` schema itself is `NOXNA`
- Main related tests: this file (Task 937); the structural counterpart to
  `easygl_model_hierarchy_child_mesh_test.cpp`'s pixel-level bone-resolution test

## Purpose

Regression test for a confirmed gap (per header comment, DEFERRED.md item #34 addendum): `ModelTypeReader::Read()`
previously created only one synthetic "Root" bone for the whole model, leaving every mesh's `ParentBone` null —
breaking game code that looks up a named per-part bone via `Model.Bones["PartName"]`. Writes a 2-mesh (`"Wheel"`,
`"Turret"`) `.cnj` fixture, loads it via `Content.Load<Model>()`, and checks: `Bones.Count == 3` (Root + one per
mesh), `Bones["Wheel"]`/`Bones["Turret"]` both resolve and are distinct, each resolved bone's `Parent` is the
model's `Root`, and each `ModelMesh`'s own `ParentBone` matches its corresponding named bone. Pure structural test,
no GPU pixel readback needed for what it's verifying. Correct placement per `AUDIT_SCOPE.md`.

## Executive Verdict

**Mostly healthy** — the assertions were independently re-derived from `ModelTypeReader::Read()`'s actual bone-
creation logic and match exactly for this fixture, but the test's own `!= nullptr` guard style
(`check(wheelBone != nullptr, ...)`) is inconsistent with the real, verified behavior of
`ModelBoneCollection::operator[](const std::string&)`, which **throws** `std::out_of_range` rather than returning
`nullptr` on a lookup miss — see Finding F1.

## Checklist Results

### API / XNA / FNA parity
`model.getBonesProperty()["Wheel"]` invokes the real XNA-shaped string-indexer overload
(`ModelBoneCollection::operator[](const std::string&)`); FNA's `ModelBoneCollection` indexer (via its own
`IEnumerable`-backed lookup) also throws `KeyNotFoundException`-equivalent behavior on a miss rather than returning
`null`, so CNA's throwing behavior is *correct* XNA parity — the mismatch identified in F1 below is between this
**test's own** defensive-null-check style and that (correct) production behavior, not a production API defect.
`model.getRootProperty()`, `ModelBone::getParentProperty()`, `ModelMesh::getParentBoneProperty()` are all real XNA
members, used correctly.

### Behavioral correctness
Traced `ModelTypeReader::Read()`'s bone-creation loop (`ContentManager.cpp` lines 2394-2411) against this exact
2-mesh fixture: one root `ModelBone` is created first (index 0, named `"Root"` since the `.cnj` has no top-level
`"bones"` array — lines 2126-2154), then for each of the 2 meshes in JSON order, a new `ModelBone` is created
(indices 1 and 2, named after the mesh — `"Wheel"`, `"Turret"`), added as a child of `rootBone` via `AddChild`
(which — per `ModelBone.cpp` line 19 — sets `child->parent_ = this`), and assigned to that mesh's
`ParentBone` via `mesh->setParentBoneProperty(meshBone.get())` (line 2409). The `Model` is then constructed via the
**3-arg** constructor (line 2562-2564, not the 4-arg one used by the hand-built
`easygl_model_hierarchy_child_mesh_test.cpp`) — `Model::Model`'s 3-arg overload (`Model.cpp` lines 15-23) sets
`root_ = bones_.bones_[0]`, i.e. the just-created `"Root"` bone, matching this test's expectation that
`wheelBone->getParentProperty() == root_bone` for `root_bone = model.getRootProperty()`. `Bones.Count == 3`
(1 root + 2 mesh bones) matches exactly.

### Logic
The mesh-name-to-`ModelMesh*` lookup loop (lines 155-160) iterates `model.getMeshesProperty()` and matches by
`getNameProperty()` string equality — correct and robust to JSON mesh-array ordering (doesn't assume `"Wheel"`
comes first), unlike a index-based lookup would be.

### Memory/resource lifetime
Standard pattern for this shard: unique per-process temp directory, `WriteTriangleFixture` helper writes minimal
valid stride-16 vertex/index files per mesh (vertex/pixel content is explicitly irrelevant to this structural test,
correctly noted in the helper's own comment). No dangling-pointer risk — `model` is a `Game::Initialize()`-local
value, and all checks run synchronously within the same function before `Exit()`.

### C++ correctness
No casts/UB.

### Performance
N/A — trivial 3-vertex/3-index fixtures per mesh, minimal test-only cost.

### Thread safety
N/A.

### Architecture
Correctly placed and scoped — a pure structural test with an intentionally-empty `Draw()` override, appropriately
not paying for a GPU frame it doesn't need.

### Maintainability
187 lines; clear separation between fixture-writing helpers and the checks themselves.

### Portability
N/A.

### Robustness
See Finding F1 — the test's error-handling style for a bone lookup miss does not match the real throwing behavior
of the API it calls.

### Testing
This is itself a test file.

### Cross-file consistency
`ModelTypeReader::Read()`'s per-mesh bone-creation logic (`ContentManager.cpp` lines 2398-2411) is shared,
unmodified code between this test and `easygl_model_json_reader_skeleton_test.cpp`/
`easygl_model_json_reader_texture_test.cpp` (same reader class) — consistent behavior expected and confirmed
across the shard.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — `!= nullptr` guard is dead code given `ModelBoneCollection::operator[](string)`'s real throwing contract

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-robustness / correctness-of-the-test-itself
- Location/symbol: `ModelBone* wheelBone = model.getBonesProperty()["Wheel"];` /
  `check(wheelBone != nullptr, "Bones[\"Wheel\"] resolves to a real bone");` (lines 143-146); contrast with
  `ModelBoneCollection::operator[](const std::string&)` (`ModelBoneCollection.cpp` lines 13-19):
  `if (TryGetValue(name, value)) return value; throw std::out_of_range(...);`
- Evidence: the string-indexer **never** returns `nullptr` on a miss — it throws. In this specific fixture the
  lookup always succeeds (both mesh names genuinely exist as bones), so the `!= nullptr` check never actually has
  a chance to be false during a normal run; it is not a real safety net against the regression it appears to guard
  against.
- Why it matters: if `ModelTypeReader::Read()`'s per-mesh bone-naming ever regressed (e.g. an empty/mismatched mesh
  name, or the bone-creation loop being skipped entirely), the operator would throw `std::out_of_range` at line
  143/144 instead of returning `nullptr`. `Game.cpp` has no top-level exception handler (confirmed in this audit
  session across the shard), so this would propagate to `std::terminate()`/abort rather than reaching this test's
  own `check()`-based `[FAIL]` reporting and pass/fail counters — every *subsequent* check in this file (lines
  147-164, including the `Turret`/`ParentBone`-matching checks) would silently never run, and the test would fail
  via a crash rather than a clean, attributable `[FAIL]` message. The test would still ultimately be detected as
  "failed" by `ctest` (non-zero/abnormal exit), so this is not a silent-false-pass risk, but it degrades the
  failure's diagnosability exactly in the regression scenario this test exists to catch.
- FNA/XNA comparison: N/A — this is a test-design observation, not an XNA/FNA parity issue (the production
  throwing behavior itself is correct XNA parity, per Checklist Results above).
- Related files: `ModelBoneCollection.hpp`/`.cpp` (own audit should independently note the throwing contract is
  correctly documented — "Throws if not found" — in the header, so this is a test-authoring oversight, not a
  documentation gap).
- Suggested future action (not implemented by this audit): use `TryGetValue()` (already public, returns `bool` +
  out-param, never throws) instead of `operator[]` for a lookup whose success is genuinely being asserted, or wrap
  the `operator[]` calls in this file's own `try`/`catch` to convert an exception into a clean `[FAIL]`.

## Cross-File Observations

- This same `!= nullptr`-after-a-throwing-lookup pattern does not appear to recur in
  `easygl_model_json_reader_skeleton_test.cpp` (that file uses `dynamic_cast` and `std::unordered_map::find`, both
  of which correctly return null/end-iterator rather than throwing) — this is specific to this file's use of
  `ModelBoneCollection`'s string indexer, not a shard-wide pattern.

## Missing or Weak Tests

- No case where a mesh name is **empty** or a duplicate of another mesh's name — `ModelTypeReader::Read()`'s own
  fallback (`meshName.empty() ? "mesh" : meshName`, `ContentManager.cpp` line 2407) means two unnamed meshes would
  both produce bones literally named `"mesh"`, which `ModelBoneCollection::operator[]`'s linear
  first-match-wins `TryGetValue` (`ModelBoneCollection.cpp` lines 26-33) would silently resolve to whichever bone
  was pushed first — this file's fixture always uses distinct, non-empty names, so that ambiguity is untested here.

## Positive Findings

- The core structural assertions (`Bones.Count`, `Parent` chain, `ParentBone` cross-reference) were independently
  re-derived from `ModelTypeReader::Read()`'s actual bone-creation loop in this audit and match exactly — this is a
  real, non-trivial regression test for a genuinely confirmed historical gap, not a placeholder.
- Correctly uses name-based (not index-based) mesh lookup, avoiding an ordering assumption the fixture doesn't
  need to make.

## Final Assessment

A genuine, well-targeted structural regression test whose core assertions are independently verified correct
against the real `ModelTypeReader`/`Model`/`ModelBone` production logic. Its one real weakness (F1) is that its own
defensive `!= nullptr` checks rely on an API contract (`operator[]` returning null on miss) that does not match
`ModelBoneCollection`'s actual, correctly-XNA-parity throwing behavior — meaning the exact regression this test is
designed to catch would surface as an uncaught-exception crash rather than the clean `[FAIL]` diagnostic the test's
own design otherwise implies.
