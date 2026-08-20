# Audit: docs/rendertarget-support.md

## Metadata
- Source file: `docs/rendertarget-support.md` (222 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (closes Phase 39, `plans/plan_graphics.md` Tasks 331-340)
- Cross-references: `xna-graphics` shard audit (191 files, 6 HIGH findings, none of which concern
  `RenderTarget2D`/`RenderTargetCube`/`Texture3D`/`TextureCube` directly — this doc's own findings
  are a disjoint, non-overlapping set)

## Purpose
Documents the per-backend (EasyGL/Vulkan/Bgfx) `RenderTarget2D`/`RenderTargetCube` conformance
audit: constructors/properties, post-unbind sampling, depth buffer functionality, mipmap/MSAA
support, `SetRenderTarget`'s viewport/scissor reset, and MRT limits — with 9 newly-opened tracked
tasks (873-881).

## Executive Verdict
An exceptionally granular, per-backend-honest support matrix — the closing summary table (§ "what
actually works today, per backend") is the clearest single artifact in this audit's documentation
review of exactly which backend supports which `RenderTarget` feature, with no feature
overstated as working when a tracked task says otherwise anywhere in the document.

## Checklist Results
- Cross-checked every ✅/❌/🔶/🔍 cell in the closing summary table against the corresponding
  numbered section's prose — fully consistent; no cell claims more than its section documents (e.g.
  the Bgfx `RenderTargetCube` sampling ❌ cell correctly cites Task 874, matching §6's wrong-handle-
  cast root-cause explanation exactly).
- §6 (Bgfx wrong-handle-type casts) gives a precise, falsifiable root-cause mechanism — both
  `RenderTarget2D`/`RenderTargetCube`'s backend classes have a *framebuffer* handle as their first
  data member where a *texture* handle is expected by the unconditional `static_cast`, and both
  handle types share the identical `struct { uint16_t idx; }` shape, so the cast compiles and
  doesn't crash but silently samples wrong data — this is a real, specific, believable mechanism, not
  a vague "sampling seems wrong" report.
- §10's finding that FNA's own `SetRenderTargets` performs zero explicit mismatched-format/size
  validation (confirmed by reading FNA's actual source, not assumed) correctly reframes what could
  look like a CNA gap as actually FNA-faithful behavior — appropriately conservative, not
  over-claiming a "bug" where none exists relative to the real API's own behavior.
- §11's MRT-cap comparison table (EasyGL/Bgfx cap at 8, Vulkan uncapped, real FNA caps at 4) is a
  clean, direct, verifiable comparison — flagged as Task 881, appropriately scoped as "not fixed" and
  "no test in this repo exercises more than 2 simultaneous targets," an honest coverage-and-fix-status
  disclosure in one line.
- §4/§5's mip/MSAA fix narratives for EasyGL both correctly and specifically distinguish "property
  reflects the right value" from "the GPU-level effect is real," with a description of the actual
  discriminating pixel test used in each case (a mip-completeness probe reusing an established
  "renders solid black on incomplete mip chains" signature; a genuine anti-aliasing differential
  check contrasting `MultiSampleCount=0` vs `=8` on a diagonal edge) — these are methodologically
  sound, not merely asserted.

## Detailed Findings
None. This document's claims are precise, falsifiable, evidence-backed, and (per the Cross-File
Observations below) do not conflict with anything else audited in the `xna-graphics` shard.

## Cross-File Observations
The `xna-graphics` shard audit (191 files, 6 HIGH findings) found no `RenderTarget2D`/
`RenderTargetCube`/`Texture3D`/`TextureCube`-specific defects among its own 6 HIGH findings — this
document's own 9 tracked findings (Tasks 873-881) occupy a genuinely disjoint area of the codebase
(per-backend `RenderTarget*` GPU wiring) from what the `xna-graphics` shard's HIGH findings covered
(`SpriteFont`/`SpriteBatch`/`EffectParameter` matrix semantics/`GraphicsException` types) — no
contradiction, no double-counting, no gap in coverage identified between the two.

## Missing or Weak Tests
The doc's own §11 explicitly states "no test in this repo exercises more than 2 simultaneous
targets" for MRT — a real, self-disclosed test-coverage gap, not contradicted by anything found in
this audit's own `tests-xna-graphics` shard review (which likewise did not encounter a >2-target MRT
test).

## Positive Findings
§10's discipline (reading FNA's actual source before concluding "no bug" rather than assuming CNA's
looser validation is automatically wrong) is a strong example of the correct audit posture this
entire project's own documentation consistently models — matching XNA/FNA behavior over "cleaner"
validation, exactly as this project's own `CLAUDE.md` "Behavior Fidelity" section prescribes. The
closing summary table's legend (✅/❌/🔶/🔍, with 🔍 specifically meaning "not empirically verified
this phase" rather than silently omitted) is a clean, honest way to distinguish "confirmed working,"
"confirmed broken," "partially correct," and "unknown" — avoiding the common failure mode of
collapsing "untested" into either "works" or "broken."

## Final Assessment
No findings. One of the most rigorous, evidence-grounded, and honestly-scoped documents reviewed in
this audit's `docs` shard — its 9 tracked findings occupy a disjoint area from the `xna-graphics`
shard's own HIGH findings, with no contradiction or gap between the two.
