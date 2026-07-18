# Audit: examples/avatar_attach_part_integration_test.cpp

## Metadata

- Source file: `examples/avatar_attach_part_integration_test.cpp`
- Audit status: AUDITED (includes empirical GPU verification of a shared code path — see below)
- Subsystem: `examples-tests-generic` shard — genuinely cross-backend
  `SkinnedModelEXT::AttachPartEXT` runtime wardrobe-attach integration test (Task 11.21).
- File type: standalone `Game`-subclass executable, CTest-registered for **two** backends
  from the same unmodified source: `cna_easygl_test(cna_test_avatar_attach_part …)` /
  `EasyGL_AvatarRenderer_AttachPart` (`cmake/Tests/EasyGLTests.cmake:246-251`, gated on
  `CNA_ENABLE_NET`) and `cna_vulkan_test(cna_test_vulkan_avatar_attach_part …)` /
  `Vulkan_AvatarRenderer_AttachPart` (`cmake/Tests/VulkanTests.cmake:709-713`). No other
  backend registers this file.
- XNA/FNA relevance: NOXNA — `SkinnedModelEXT::AttachPartEXT` and the whole real-rendering
  Avatar extension are CNA-only (see class remarks in `SkinnedModelEXT.hpp`/
  `AvatarRenderer.hpp`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.cpp`
  (`AttachPartEXT` lines 109-142, `RemovePartEXT` lines 73-107), `src/Microsoft/Xna/Framework/
  GamerServices/AvatarRenderer.cpp` (`DrawRealEXT` lines 178-228).

## Purpose

Builds two independent one-bone-quad `SkinnedModelEXT`s ("host", red, NDC `x:-1..0`;
"wardrobe", blue, NDC `x:0..1`), calls `host->AttachPartEXT(std::move(*wardrobe))`, then draws
only `host` and checks both halves render their respective original colors, plus
`host->Parts.size() == 2` — proving a second, independently-loaded model's part can be
transplanted into a running model's own draw list at runtime, exercising the exact
buffer-ownership-transfer path a converted wardrobe piece would use (per the header comment's
own framing, this is deliberately real-world-shaped, not an artificial API-surface check).

## Checklist Results

### Behavioral correctness
Independently traced `SkinnedModelEXT::AttachPartEXT()` (`SkinnedModelEXT.cpp` lines
109-142) against this exact call: `other.BoneCount (1) == this->BoneCount (1)` → no throw;
the replace-by-name loop (`for (const auto& part : other.Parts) RemovePartEXT(part.Name);`)
iterates `other.Parts = ["wardrobe"]`, calling `RemovePartEXT("wardrobe")` against `host`,
whose own `Parts` only contains `"host"` — no match, no removal, correctly a no-op for this
scenario (the two models use different part names by design, so the "same-name replacement"
path this loop exists for is not exercised here — see Missing or Weak Tests). The subsequent
append loop transfers `wardrobe`'s single `PartEXT` plus its one owned `VertexBuffer`/
`IndexBuffer`/`ModelMeshPart`/(no-op, no texture entry since `AddPartEXT` only populates
`textures_` `if (texture.HasBackend())`, and both test textures are real 1×1 textures so both
*do* get a `textures_` entry) into `host`'s own parallel arrays — `host->Parts.size()`
becomes `2`, matching the test's `partCountOk` check exactly.

### Logic
`AttachPartEXT`'s own doc comment states it is a **CNA extension requiring both models share
"this model's exact bone count and index order"** and explicitly does *not* attempt to merge
or validate `Clips` — confirmed by reading the method body: only `Parts`/`vertexBuffers_`/
`indexBuffers_`/`ownedParts_`/`textures_` are touched; `Clips` is never referenced. This test
builds *separate*, textually-identical `"Test"` clips on both `host` and `wardrobe`
(`BuildOneBoneQuadModel()`'s own per-model clip construction, lines 58-64) rather than relying
on any clip transfer from `AttachPartEXT` — meaning after the attach, `host->DrawRealEXT("Test",
…)` uses **`host`'s own pre-existing `"Test"` clip** (never wardrobe's, which is simply
discarded along with the rest of `other`'s now-empty state) to drive bone 0, which both
models' `"Test"` clips happen to define identically here (single keyframe, translation
`(0,0,0)`, i.e. no motion) — so this test cannot distinguish "wardrobe's clip data was
correctly ignored/discarded" from "wardrobe's clip data was silently expected to matter and
wasn't." Not a defect in `AttachPartEXT` (its own doc is explicit that clip merging is out of
scope, and a shared skeleton implies a shared clip vocabulary by construction) but a genuine
test-coverage gap for the "what happens if a caller only ever built one clip and expects both
models' vertices to move under it" question a real wardrobe piece would actually face — see
Missing or Weak Tests.

### Memory/resource lifetime
`host->AttachPartEXT(std::move(*wardrobe))` — `wardrobe` is a `std::shared_ptr<SkinnedModelEXT>`;
`*wardrobe` dereferences to the pointee, moved-from via `AttachPartEXT(SkinnedModelEXT&&)`.
After the call, `wardrobe`'s pointee is left in the documented moved-from state (empty
`Parts`/owned-resource vectors, per `AttachPartEXT`'s own clearing of `other.*` at the end of
the method) while the `shared_ptr` itself remains valid and non-null — `wardrobe` (the local
variable) is never dereferenced again after this line, so the moved-from state is never
observed; no use-after-move issue. `host`'s subsequently-transferred `VertexBuffer`/
`IndexBuffer` unique_ptrs are correctly re-homed into `host`'s own `vertexBuffers_`/
`indexBuffers_` (verified via `AttachPartEXT`'s move-loop, lines 134-137) — no double-free or
dangling-pointer risk from the ownership transfer.

### Robustness
Both `leftOk`/`rightOk` checks (lines 140-143) use one-sided inequalities
(`R>G && R>B`, `B>R && B>G`) rather than exact color matching — appropriately tolerant of
ordinary lighting variance while still being a meaningful, non-trivial discriminator (a
routing bug that rendered both halves the same color, or swapped them, would fail one or
both checks). Unlike the sibling `avatar_tint_routing_integration_test.cpp` (this batch), this
file does **not** use a white texture with a tight tolerance against an exact expected
color — it uses fully-saturated red/blue textures (`redTex_`/`blueTex_`, `Initialize()` lines
99-100) with loose channel-dominance checks, which (per the same reasoning traced in
`avatar_real_render_integration_test.cpp.audit.md`'s F1) makes this file **insensitive** to
the confirmed ambient/emissive-forwarding defect in `SkinnedEffect`'s Vulkan/generic path —
whatever the lighting magnitude actually is, a red-textured quad's `G`/`B` channels stay at
`0` and a blue-textured quad's `R`/`G` channels stay at `0`, so the dominance checks hold
regardless.

### Testing
This file's actual, provable claim — `AttachPartEXT` correctly transfers ownership and both
the pre-existing and newly-attached part render in the same draw call — was independently
confirmed via source-tracing (above) rather than by re-executing this specific file. This
batch's build+run of the structurally-identical sibling `avatar_tint_routing_integration_test.cpp`
(see that file's own report) already empirically demonstrated that `AvatarRenderer::DrawRealEXT`'s
shared lighting path is currently measurably miscalibrated on EasyGL — but, per the Robustness
section above, this file's own assertions are shaped so as not to detect that, so its passing
status is expected and not undermined by that finding.

### Cross-file consistency
`BuildOneBoneQuadModel()` here takes an extra `partName`/`ndcXMin`/`ndcXMax` parameter set
compared to the near-identical helper in `avatar_real_render_integration_test.cpp` — a minor,
reasonable per-file variation (this file needs two independently-named, independently-placed
models; that file needs only one). Both otherwise share the same `BindPoseLocal=Identity`/
`InverseBindPoseGlobal=Identity`/single-keyframe-clip construction shape.

## Detailed Findings

No CRITICAL/HIGH findings for this file's own code. One MEDIUM test-coverage gap (clip
handling across `AttachPartEXT`) and one LOW/INFO note mirroring the sibling files' shared
insensitivity to the confirmed lighting defect.

### F1 — `AttachPartEXT`'s deliberate non-handling of `Clips` is untested from the "attached part expects motion the host doesn't define" angle

- Severity: MEDIUM
- Confidence: HIGH (confirmed by direct reading of `AttachPartEXT`'s body — `Clips` is never
  referenced; confirmed this test's own two clips are constructed identically, masking the
  question)
- Category: test-coverage
- Location/symbol: `SkinnedModelEXT::AttachPartEXT()` (`SkinnedModelEXT.cpp` lines 109-142,
  no `Clips` handling); this test's `BuildOneBoneQuadModel()` (lines 49-84, both host and
  wardrobe given identical single-keyframe "Test" clips)
- Why it matters: a real wardrobe piece converted via `convert_avatar.py` (per the header
  comment's own framing of what this test mirrors) is unlikely to ship its own
  `AnimationClipEXT` data at all — it shares the host avatar's skeleton and is driven by
  whatever clip the host is already playing. This test's choice to give *both* models their
  own identical clip means it cannot distinguish "the attached part's vertices correctly move
  under the host's existing clip because they share a skeleton and bone index space" (the
  actual, intended real-world mechanism) from "the attached part happened to carry its own
  compatible clip data that was never actually used." A regression that broke bone-index
  correspondence between attached parts (e.g. if a future change made `AttachPartEXT`
  remap or renumber incoming vertex bone indices incorrectly) would only be caught if the
  motion applied by the host's clip were visibly different from a no-op — which a
  translation-only, single-keyframe, zero-displacement clip (as used by both models here)
  cannot reveal, since the wardrobe part's vertices would render correctly-positioned even
  under a completely wrong bone-index mapping (both bone 0 slots being identity transforms
  either way).
- FNA/XNA comparison: N/A (NOXNA extension).
- Suggested future action (not implemented by this audit): give the *host* model alone a
  clip with a non-zero bone-0 translation (or, better, a 2-bone host where the wardrobe part
  is skinned partially to bone 1) and confirm the attached wardrobe part visibly moves under
  that motion despite never having defined a "Test" clip of its own — this would be a much
  stronger proof of the "shares the host's skeleton and clip vocabulary" claim the header
  comment makes.

## Cross-File Observations

- Shares the `RasterizerState::CullNone` Task-896 comment and overall scaffolding shape with
  every other file in this batch — see `alpha_test_integration_test.cpp.audit.md`'s
  Cross-File Observations.
- Together with `avatar_real_render_integration_test.cpp` and
  `avatar_tint_routing_integration_test.cpp`, this is one of only three files in the entire
  `examples/` tree registered against two different graphics backends from one unmodified
  source — see the tint-routing file's report for the concrete cross-backend divergence this
  property allowed this audit to uncover in the sibling file.
- `RemovePartEXT`'s replace-by-name behavior (exercised, per the doc comment, precisely for
  the "swap hairstyles at runtime" scenario) is present in the production code this test
  calls but is a documented no-op for this specific test's chosen part names (`"host"` vs.
  `"wardrobe"` never collide) — the *actual* replace-by-name path has no coverage in this
  file; check whether `SkinnedModelEXTTests.cpp` (under `tests/`, out of this batch's scope)
  covers it directly.

## Missing or Weak Tests

- See F1 — no coverage of an attached part's vertices being driven by the *host's* clip data
  when the attached part itself carries no (or incompatible) clip data of its own.
- No coverage of the `AttachPartEXT` bone-count-mismatch throw path (`other.BoneCount !=
  BoneCount`) in this file (plausibly covered elsewhere in `tests/`, out of scope for this
  batch, but not cross-checked here).
- Per the Robustness section, no variant of this scenario would detect the confirmed
  ambient/emissive-forwarding defect in `SkinnedEffect`'s shared lighting path (see
  `avatar_tint_routing_integration_test.cpp.audit.md`) — this file's red/blue fully-saturated
  textures make it structurally insensitive to that class of bug.

## Positive Findings

- The core ownership-transfer mechanics of `AttachPartEXT` — parallel-array consistency
  across `Parts`/`vertexBuffers_`/`indexBuffers_`/`ownedParts_`/`textures_`, and the
  replace-by-name pre-pass — were independently traced against the current production
  implementation and confirmed correct for this test's exact scenario.
- Good, realistic test framing: explicitly modeled on the actual runtime wardrobe-attach
  workflow (`generate_wardrobe.py`/`convert_avatar.py`) rather than an abstract API-only
  check, per the header comment.
- Correctly checks `host->Parts.size() == 2` in addition to pixel colors — a cheap,
  meaningful structural assertion that a pixel check alone would not provide (e.g. it would
  catch a regression that rendered the correct colors via some other path while silently
  failing to actually grow `Parts`).

## Final Assessment

Mostly healthy. The ownership-transfer logic this file's name promises to test was
independently confirmed correct; F1 identifies a real but narrow gap in what the test can
distinguish (clip-sharing vs. clip-carrying for an attached part) rather than a defect in the
file as written.
