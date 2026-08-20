# Audit: examples/easygl_bloom_extract_test.cpp

## Metadata

- Source file: `examples/easygl_bloom_extract_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`EasyGLBloomExtractTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Content::ContentManager::EffectTypeReader`
  (`ContentManager.cpp` lines 715-785, specifically `ReadCustomGlslEffect` lines 765-785),
  `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.cpp`).
- XNA/FNA relevance: exercises `ContentManager::Load<T>()` (real XNA 4.0 API) end-to-end against a genuine `.cnj`
  content descriptor — the CNA-specific content-pipeline replacement for XNB (see `plans/plan_cnj.md`, out of this
  batch's scope but referenced by this file's own header). The pixel-shader math itself (`BloomExtract.fx`) is from
  the Microsoft XNA Game Studio **BloomPostprocess** sample, not part of FNA.
- FNA reference: N/A for the shader body — the `BloomSample_4_0` sample project does not exist anywhere under
  `/rv/data/library/github.com/FNA-XNA/FNA` (confirmed by search; only FNA's own stock-effect `.fx` files are
  present). Internal consistency (comment HLSL → GLSL → hand-computed pixel) was checked instead.
- Main related tests: sibling of `easygl_bloom_combine_test.cpp`, `easygl_bloom_gaussianblur_test.cpp`,
  `easygl_bloom_pipeline_test.cpp`.

## Purpose

Task 946 shader-conversion proof for `BloomExtract.fx`'s threshold-remap pixel shader, plus — per the file's own
header (lines 18-22) — the **first exercise anywhere in the repository** of the `.cnj`-based `Effect` content-type
round trip through `ContentManager::Load<std::shared_ptr<Effect>>()` (migrated from `.shader.json` to `.cnj` per
`plans/plan_cnj.md` CNB-14/CNB-15). Correctly placed as an `easygl_`-prefixed integration test per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — both of this file's two real claims (the threshold-remap math, and the `.cnj` Effect-loading round
trip) were independently verified against production code and are accurate; the test's own math is exact rather
than approximate for both checks, and the content-pipeline path it exercises was confirmed to genuinely require the
exact `.cnj` shape (`"type": "Effect"`, `"vertex"`, `"fragment"`) this file writes.

## Checklist Results

### API / XNA / FNA parity
`ContentManager::Load<T>()` (real XNA 4.0 API) and `ContentManager::setRootDirectoryProperty()` (CNA's
getX/setX mapping of XNA's `ContentManager.RootDirectory`) are used correctly. The `.cnj` extension and JSON schema
(`cnjVersion`, `type`, `vertex`, `fragment`) are CNA-specific (`NOXNA`, replacing XNB) — correctly not XNA-namespace
API surface, just a loader input format.

### Behavioral correctness
Traced `EffectTypeReader::Read()` (`ContentManager.cpp:723-762`): reads the `.cnj` JSON, parses its envelope, checks
`envelope.type` is one of the 6 valid names (line 743-753) *before* any other validation (a deliberately-ordered
check per the code's own "found by adversarial review" comment at line 742) — the test's own `.cnj` (`"type":
"Effect"`) takes the `ReadCustomGlslEffect()` branch (line 757-759), which extracts `"vertex"`/`"fragment"` string
fields (lines 768-769), resolves them relative to `cm.getRootDirectoryProperty()` (lines 778-780), and constructs a
real `ShaderEffect` from their file contents (lines 782-785) — this exactly matches what the test's `Initialize()`
sets up (lines 110-118: writes `bloom_extract.vert.glsl`, `bloom_extract.frag.glsl`, and a `.cnj` referencing both
by relative filename, then sets `setRootDirectoryProperty(root.string())` before calling `Load<...>("bloom_extract")`).

Hand-verified both pixel checks independently against the ported formula
`clamp((c - uBloomThreshold) / (1.0 - uBloomThreshold), 0.0, 1.0)`:
- Check A: bright `(1,1,1,1)`, threshold `0.5` → `clamp((1-0.5)/0.5,...)=1` on every channel including alpha →
  `(255,255,255,255)` — matches `brightOk` (all channels `>=250`, lines 162-163).
- Check B: dim pixel value used is `76` (`76/255 ≈ 0.298`), threshold `0.5` → RGB:
  `clamp((0.298-0.5)/0.5,0,1)=clamp(-0.404,0,1)=0`; alpha (stored as `255`→`1.0`):
  `clamp((1-0.5)/0.5,0,1)=1` → `(0,0,0,255)` — matches `dimOk` (RGB `<=5`, alpha `>=250`, lines 164-165).
  Both are exact (not near-boundary) results, so the `<=5`/`>=250` tolerances are comfortably conservative, not
  hiding a marginal pass.

### Logic
Linear `Draw()` guarded by `done_`: validate loaded effect → clear blue → apply → set threshold uniform → draw two
side-by-side quads (bright half, dim half) → read back one pixel from each half → compare → `Exit()`. No branches
beyond the `!fx || !fx->IsEffectValid()` guard (lines 137-142).

### Memory/resource lifetime
`fxBase_` is `std::shared_ptr<Effect>` owned by the test, obtained via `ContentManager::Load` — correct ownership
transfer, no raw-pointer risk. The temp content directory (`std::filesystem::temp_directory_path() /
"cna_bloom_extract_test_<this-ptr>"`) is created but never explicitly removed after the test — a minor, low-impact
leftover-temp-file concern (same pattern likely repeats across every `.cnj`-writing test in this shard; see Missing
or Weak Tests) rather than a defect specific to this file.

### C++ correctness
`dynamic_cast<ShaderEffect*>(fxBase_.get())` (line 136) correctly downcasts and null-checks (line 137) before use —
matches the polymorphic `Effect`/`ShaderEffect` relationship established elsewhere in the codebase (`EffectTypeReader`
returns `std::shared_ptr<Effect>`, but this test needs `ShaderEffect`'s `NOXNA` `SetUniformFloat`/`IsEffectValid`
surface).

### Performance
N/A — single-frame test.

### Architecture
Correctly exercises only the public `ContentManager`/`Effect`/`ShaderEffect` surface; no direct backend coupling.

### Robustness
`WriteFile()` (lines 56-60) doesn't check `ofstream` success, and the `.cnj`/GLSL writes have no error handling —
acceptable for a controlled test-fixture writer (temp dir just created via `create_directories`), not a robustness
gap worth flagging at test-fixture-authoring severity.

### Testing
This file is itself a test; no coverage gaps of note beyond the general temp-directory-cleanup observation above.

## Detailed Findings

No MEDIUM-or-higher findings. One LOW/INFO item:

### F1 — Temp content directory is never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource hygiene
- Location/symbol: `Initialize()` lines 103-106 (`std::filesystem::create_directories(root)`), no matching
  `remove_all` anywhere in the file
- Evidence: the per-run temp directory (keyed by `this` pointer, so unique per process run) is created but never
  removed, in `Initialize()`, `Draw()`, or a destructor.
- Why it matters: harmless for a single CI run, but accumulates stray `/tmp/cna_bloom_extract_test_*` directories
  across repeated local test runs; purely a hygiene nit, not a correctness issue.
- FNA/XNA comparison: N/A.
- Related files: the same pattern appears in `easygl_blur_shader_test.cpp` and
  `easygl_cartooneffect_lambert_shader_test.cpp` in this same batch (see Cross-File Observations) — a shard-wide,
  not file-specific, pattern.
- Suggested future action (not implemented by this audit): add a destructor or end-of-`Draw()`
  `std::filesystem::remove_all(root)` if temp-directory accumulation becomes a real nuisance in CI.

## Cross-File Observations

- The uncleaned temp-directory pattern (F1) is shared verbatim across all `.cnj`-content-pipeline tests in this
  batch (`easygl_blur_shader_test.cpp`, `easygl_cartooneffect_lambert_shader_test.cpp`) — worth a single shared
  helper (e.g. an RAII temp-dir guard) if/when any of these files is touched again, rather than fixing this one
  file in isolation.
- This is the first file in this batch (chronologically, per its own header) to exercise the `.cnj` Effect content
  path that `easygl_blur_shader_test.cpp` and `easygl_cartooneffect_lambert_shader_test.cpp` both also rely on —
  all three independently confirm the same `ReadCustomGlslEffect()` code path works, which is good redundant
  coverage of a shared dependency rather than duplicated effort for its own sake (each test's actual shader/math
  content differs).

## Missing or Weak Tests

- No test in this file (or, from this batch, its siblings) exercises the `EffectTypeReader`'s error paths for a
  malformed `Effect` `.cnj` (missing `vertex`/`fragment` field, line 771-775 of `ContentManager.cpp` throws
  `ContentLoadException`) — reasonable to leave to a dedicated `ContentManager`/`.cnj` parsing test file rather than
  every consumer of the format, so not treated as a gap specific to this file.

## Positive Findings

- Both pixel-check derivations are exact algebra (not approximations), independently re-derived and confirmed
  correct during this audit — a well-authored, non-boilerplate test.
- Genuinely exercises a real, previously-unexercised production code path (the `.cnj` Effect content-type reader)
  rather than just re-testing `ShaderEffect`'s direct-source constructor already covered by sibling tests.

## Final Assessment

An accurate, well-verified test of both its stated claims: the `BloomExtract.fx` threshold-remap math (exact,
correctly derived) and the `.cnj`/`ContentManager` Effect-loading round trip (traced end-to-end against
`EffectTypeReader::Read()`/`ReadCustomGlslEffect()` and found to match exactly). Only a cosmetic temp-directory
hygiene nit, shared with two sibling files, keeps this from a perfect score.
