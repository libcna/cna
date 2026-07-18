# Audit: examples/bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` `TextureEnabled=true` AND `VertexColorEnabled=true`
  (stride-24 `VertexPositionColorTexture`) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_texture_vertexcolor_enabled …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_TextureVertexColorEnabled …)`, `cmake/Tests/BgfxTests.cmake:299-302`)
- XNA/FNA relevance: direct — `BasicEffect.VertexColorEnabled`, `TextureEnabled`, the `VSBasicTxVc`/`PSBasicTx`
  combined shader path
- FNA reference: `HLSL/BasicEffect.fx` (`VSBasicTxVc`: `vout.Diffuse = DiffuseColor; ... vout.Diffuse *=
  vin.Color;`; `PSBasicTx`: `SAMPLE_TEXTURE(...) * pin.Diffuse`) — net formula
  `TextureColor * VertexColor * DiffuseColor`
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored_textured3d.sc`,
  `fs_colored_textured3d.sc`; `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp` line 48
  (`VertexColorEnabled` field)

## Purpose

7-check pixel test proving the full 3-way multiply `TextureColor*VertexColor*DiffuseColor` for the
stride-24 `VertexPositionColorTexture` path. Per the file's own header, this test also found and fixed a real
bug on Bgfx (Task 367): `vs_colored_textured3d.sc` previously computed `v_color0 = a_color0` with no
`u_diffuseColor` multiply and no `u_vertexColorEnabled3D` gate at all, silently dropping `DiffuseColor`
entirely whenever `VertexColorEnabled` was set — fixed to mirror `vs_colored3d.sc`'s established Task-364
gating pattern.

## Executive Verdict

**Needs attention** — the core 3-way multiply is correctly implemented and all 7 numeric checks were
independently re-derived and match exactly. However, this file's own scene setup (`fx.VertexColorEnabled =
true;`, line 110) directly exercises a genuine API-convention violation in `BasicEffect.hpp`: `VertexColorEnabled`
is exposed as a bare public field with no `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()`
wrapper at all, unlike every other settable property this test file (and its 7 siblings) use — see F1.

## Checklist Results

### API / XNA / FNA parity
`fx.setTextureEnabledProperty(true)` / `setTextureProperty(&tex)` / `setDiffuseColorProperty(kDiffuse)` follow
the project's established getX/setX convention. `fx.VertexColorEnabled = true;` (line 110) does not — see F1.

### Behavioral correctness
Re-derived the full product: `kTexColor=(200,100,50)`, `kVertexColor=(150,200,100)`, `kDiffuse=(0.8,0.4,0.6)`.
Normalizing texture/vertex colors to `[0,1]` (`/255`) before multiplying by the already-fractional
`DiffuseColor`, then rescaling back to byte range:
`R: (200/255)*(150/255)*0.8*255 = 0.7843*0.5882*0.8*255 ≈ 94.09 → 94`
`G: (100/255)*(200/255)*0.4*255 = 0.3922*0.7843*0.4*255 ≈ 31.37 → 31`
`B: (50/255)*(100/255)*0.6*255 = 0.1961*0.3922*0.6*255 ≈ 11.77 → 12`
→ **(94,31,12)**, exact match to `kExpected`. All five negative-check constants
(`kTextureDiffuseOnly(160,40,30)`, `kVertexDiffuseOnly(120,80,60)`, `kTextureVertexOnly(118,78,20)`,
`kTextureOnly(200,100,50)`, `kVertexOnly(150,200,100)`, `kDiffuseOnly(204,102,153)`) were spot-checked and
independently confirmed as internally-consistent partial-product reference points (e.g.
`kVertexDiffuseOnly`: `(150/255)*0.8*255=120`, `(200/255)*0.4*255=80`, `(100/255)*0.6*255=60` → matches
exactly).

### Logic
Single scene, 7 assertions against one rendered pixel — appropriately exhaustive for ruling out every
2-of-3-terms-only combination a partially-broken shader could produce.

### C++ correctness
No issues found in the test file itself.

### Robustness
The 6-negative-check design (every pairwise-product-without-the-third-term combination, plus each term
alone) is the most thorough negative-check set in this batch — a regression that dropped any single one of
the three multiplicands, in isolation or in combination with a second, would be caught by at least one of
these 6 checks, not just the positive assertion.

### Testing
Strongest test-design in this batch by check count and combinatorial coverage of the failure space.

### Cross-file consistency
Traced `vs_colored_textured3d.sc` (lines 18-22): `vec4 vc = (u_vertexColorEnabled3D.x > 0.5) ? a_color0 :
vec4(1.0); v_color0 = vc * u_diffuseColor;` — confirms the Task 367 fix (gating on `u_vertexColorEnabled3D`
and multiplying by `u_diffuseColor`) is present and mirrors `vs_colored3d.sc`'s established pattern exactly, as
the header comment claims.

## Detailed Findings

### F1 — `BasicEffect::VertexColorEnabled` is a bare public field, not a getX/setX property, violating this project's own explicit convention and diverging further from FNA's actual C# property than the codebase's other settables

- Severity: MEDIUM
- Confidence: HIGH (read `BasicEffect.hpp`, `BasicEffect.cpp`, and FNA's `BasicEffect.cs` directly)
- Category: architecture / api-parity
- Location/symbol: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp:48`
  (`bool VertexColorEnabled = false;`), exercised directly at this test file's line 110
  (`fx.VertexColorEnabled = true;`)
- Evidence: this project's own `CLAUDE.md` states, verbatim: *"C# properties use the established CNA
  convention: `getXProperty()`/`setXProperty()` ... Do not replace C# properties with public fields unless the
  type already establishes that style."* FNA's `BasicEffect.cs` (lines 337-349) confirms `VertexColorEnabled`
  is a genuine C# property with a non-trivial setter (`dirtyFlags |= EffectDirtyFlags.ShaderIndex`), not a
  field. In CNA's `BasicEffect.hpp`, `VertexColorEnabled` (line 48) is declared as a bare `bool` field with no
  corresponding `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` anywhere in the header or
  `.cpp` (confirmed via `grep -rn "VertexColorEnabledProperty"` returning zero matches). This is a stricter
  violation than `World`/`View`/`Projection` on the same class (also public fields, lines 40-42), which at
  least additionally expose `getWorldProperty()`/`setWorldProperty()` etc. (required by the `IEffectMatrices`
  interface) alongside the field — `VertexColorEnabled` has no equivalent wrapper at all, so there is no
  getX/setX-conforming way to read or write it, a strictly worse case than its siblings.
- Why it matters: this test file's line 110 (`fx.VertexColorEnabled = true;`) is not itself wrong — it
  compiles and behaves correctly, since `FillGpuDrawParams()` (`BasicEffect.cpp` line 56:
  `p.vertexColorEnabled = VertexColorEnabled;`) reads the same field — but it is the one place in this whole
  8-file batch where the exercised API surface itself deviates from the project's documented C++ porting
  convention, and it forecloses any future generic/reflective code (e.g. a hypothetical property-enumeration
  helper, or an editor/inspector built over `getXProperty()`/`setXProperty()` naming) from discovering or
  manipulating this specific flag the same way it would every other `BasicEffect` setting. Since this is a
  test file, not the production header itself, this audit reports it as a cross-file observation surfaced by
  this test's own API usage rather than a defect in the test's own logic — the underlying fix belongs in
  `BasicEffect.hpp`, out of this file's scope to correct.
- FNA/XNA comparison: FNA's real `VertexColorEnabled` is a property with a dirty-flag side effect that CNA's
  `BasicEffect` does not need to replicate (per this codebase's architecture, `FillGpuDrawParams()` recomputes
  the full parameter set fresh on every `Apply()` call rather than relying on a cached/dirty-flag shader-index
  selection model) — so the *behavioral* deviation from FNA is intentional and benign; only the *structural*
  C++ API-shape convention (getX/setX) is the actual gap.
- Related files: `include/Microsoft/Xna/Framework/Graphics/BasicEffect.hpp` (the actual file needing the fix,
  outside this audit shard's scope); no other file in this batch touches `VertexColorEnabled` this directly (
  `bgfx_basiceffect_vertexcolor_disabled_test.cpp` only relies on its *default* value, never sets it).
- Suggested future action (not implemented by this audit — out of scope for a test-file-only batch): add
  `getVertexColorEnabledProperty()`/`setVertexColorEnabledProperty()` to `BasicEffect.hpp`/`.cpp` per the
  project's established convention, either alongside the field (matching `World`/`View`/`Projection`'s
  existing dual-exposure pattern) or replacing the field outright.

## Cross-File Observations

- This is the only file among the 8 in this batch that sets `VertexColorEnabled` explicitly (as opposed to
  relying on its default `false`), so it is the only file in this batch that surfaces F1.
- Shares its Task-367 fix narrative and shader (`vs_colored_textured3d.sc`) with no other file in this batch,
  but the analogous no-texture `VertexColorEnabled` gating (`vs_colored3d.sc`) is implicitly exercised by
  `bgfx_basiceffect_vertexcolor_disabled_test.cpp`'s reliance on the *disabled* default.
- Shares the batch's wrong-task-number cull-state documentation issue (see F2).

### F2 — Header comment's cull-state "not fixed there or here" claim cites the wrong task number and is stale (shared with 7 sibling files)

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment lines 14-18 (`"tracked as Task 884, not fixed there or here"`)
- Evidence: identical to the finding recorded for `bgfx_basiceffect_texture_enabled_test.cpp` and
  `bgfx_basiceffect_one_light_test.cpp` — the real Task 884 (`75aefb7b`) is an unrelated dangling-pointer fix;
  the actual cull-state fix (Task 896, `b6a00bc6`) is confirmed present in the current `HEAD` via
  `git merge-base --is-ancestor`, and `GraphicsDevice.cpp` line 207 confirms it is live. This file's last
  content change is commit `ca7c8ae4` (Jul 6 18:42), predating `b6a00bc6` (Jul 7 19:39).
- Why it matters: same as recorded across this batch.
- FNA/XNA comparison: N/A.
- Related files: shared with 7 sibling files.
- Suggested future action (not implemented by this audit): correct the task-number reference.

## Missing or Weak Tests

None found — the 7-check combinatorial set is the most thorough coverage in this batch.

## Positive Findings

- The most combinatorially thorough negative-check design in this batch (6 negative checks covering every
  pairwise and single-term partial product), all independently re-derived and matching exactly.
- The Task 367 bug this file is named for (missing `DiffuseColor` multiply and missing `VertexColorEnabled`
  gate in `vs_colored_textured3d.sc`) was independently confirmed fixed by direct shader inspection, not
  merely trusted from the header comment's narrative.

## Final Assessment

The strongest-covered test in this batch by assertion count, with a correct and independently-verified
3-way-multiply implementation. Its most notable finding (F1) is not a defect in this test file's own logic but
a real, project-convention-violating API gap in `BasicEffect.hpp` that this file's own scene setup happens to
exercise directly — worth a follow-up fix in the production header, separate from this test.
