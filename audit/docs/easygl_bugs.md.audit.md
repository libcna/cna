# Audit: docs/easygl_bugs.md

## Metadata
- Source file: `docs/easygl_bugs.md` (98 lines)
- Audit status: AUDITED (full read + targeted source cross-check)
- Subsystem: `docs` shard
- File type: Markdown documentation (living bug-tracking doc)
- XNA/FNA relevance: documents confirmed bugs/limitations in
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` — EasyGL is this project's most
  mature backend, fully audited this session (`backend-easygl`/`cna-graphics`/`xna-graphics` shards,
  no HIGH findings against EasyGL specifically)
- Cross-checked directly: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (fog shader
  source, lines ~2590-2740)

## Purpose
Living bug list for EasyGL: confirmed pre-existing test failures, GLES3/platform constraints,
incorrect/divergent behavior, missing features, and viewport/coordinate-system notes.

## Executive Verdict
**The document's own staleness banner (added 2026-07-11) is correct to warn readers, and this audit
found the warning is not merely precautionary — at least one additional row beyond the 2 the banner
already corrected is now demonstrably stale.** The "fog shaders — diverges" row claims: "Fog is
computed on `aPos.z` **after the WVP transform** (clip-space Z), not on view-space depth." Direct
inspection of the current `EasyGLGraphicsBackend.cpp` fog shader source shows the vertex shader
formula `vFogFactor = ... clamp((aPos.z+uFogEnd)/(uFogEnd-uFogStart), 0.0, 1.0)`, where `aPos` is
declared as an `in` vertex **attribute** — i.e. the raw incoming per-vertex position as received by
the vertex shader, which is **object/local space**, not "after the WVP transform" (clip-space). The
adjacent source comment explicitly states this formula is "Task 1111: matches FNA's
`EffectHelpers.SetFogVector`/`Common.fxh ComputeFogFactor` exactly... dotted with object-space
[position]" — the code and its own comment agree the input is object-space, contradicting this
doc's specific "after the WVP transform (clip-space Z)" characterization. Task 1111 postdates this
doc's own last-touched task (227) and its 2026-07-11 spot-check by a wide margin, strongly
suggesting the fog implementation was reworked (likely as part of the fog fixes documented in
`docs/dualtextureeffect-support.md`/`docs/environmentmapeffect-support.md`'s Task 899, "real fog now
implemented across every 3D shader on both [Vulkan/Bgfx] backends") after this specific row was
last verified, exactly the kind of drift the doc's own banner warns about ("not exhaustively
re-verified... treat any row not marked 'still confirmed 2026-07-11' with appropriate caution").

## Checklist Results
- The document's own status banner (2026-07-11) already caught and corrected 2 stale rows
  (`TextureFilter::Anisotropic`, fixed by Task 918; `CreateRenderTargetCube`'s `hasDepth` bool,
  reworked to a real `depthFormat` parameter) — a genuinely good self-correcting practice, and this
  audit's own fog-row finding is the same class of correction the banner already models.
- Several rows explicitly marked "still confirmed 2026-07-11" (`preserveContents` ignored in
  `CreateRenderTarget2D`; `UpdatePixelsLevel` never calls `generate_mipmap`) were spot-checked at
  that date and are presented with appropriate confidence — this audit did not attempt to
  independently re-verify every one of those given time constraints, but their explicit
  "still confirmed" marking is itself the right signal to trust them more than an unmarked row.
- The `SetRenderTargets()` MRT bug (`mrtFboReady_` reset causing FBO leak on every bind) and the
  `glDrawElementsBaseVertex` extension-check gap are specific, plausible, file:line-cited claims not
  independently re-verified in this pass but not contradicted by anything found elsewhere this
  session.

## Detailed Findings

### MEDIUM — "fog shaders — diverges" row's technical characterization ("after the WVP transform, clip-space Z") is contradicted by current source, which computes fog from a plain object-space vertex attribute
See Executive Verdict above for the full evidence. Whether the *underlying behavior* (accuracy vs.
FNA) is now fixed, unchanged, or differently-broken cannot be fully settled from a docs-only audit
pass without a dedicated EasyGL-backend fog re-audit (the `backend-easygl`/`cna-graphics`/
`xna-graphics` shards' own audits this session did not specifically re-examine this exact fog
formula in isolation) — but the doc's specific *description* of which coordinate space the bug
operates in is factually inconsistent with the current source's own explicit "object-space" comment,
regardless of correctness. This is exactly the kind of row the document's own 2026-07-11 banner
warns readers not to trust without independent verification.

## Cross-File Observations
Directly ties to a standing session-memory item (`feedback_easygl_fog_object_space_only.md`):
"fog shader reads raw local vertex Z, ignores World/View entirely; bake distance into vertex data,
don't rely on World translation" — that characterization ("object-space") is consistent with what
this audit's direct source read found (`aPos.z`, a raw attribute), and is a different, more precise
description than this document's own "clip-space Z, after the WVP transform" — reinforcing that this
doc's row is the stale one, not the memory record.

## Missing or Weak Tests
Not directly assessed in this pass — a dedicated EasyGL fog pixel-verification test (if any exists)
was not cross-checked against the current shader formula here; that would be the natural follow-up
to close this finding out conclusively.

## Positive Findings
The document's own 2026-07-11 status banner is a genuinely good, rare practice: rather than silently
leaving a stale bug-tracking doc to rot, it explicitly flags itself as partially unverified and
corrects the 2 rows it did re-check — this audit's own fog-row finding is best read as validating
that the banner's caution is well-founded, not as a criticism of the document's maintenance
practice.

## Final Assessment
One MEDIUM finding: the "fog shaders" row's specific technical claim about which coordinate space
CNA's EasyGL fog formula operates in is contradicted by current source and its own adjacent code
comment. Recommend either re-verifying and correcting this row (matching the doc's own precedent
for the Anisotropic/CreateRenderTargetCube corrections) or, if the fog implementation has
genuinely moved since Task 227, marking it "superseded, needs full audit" rather than presenting it
as a current, confirmed bug description.
