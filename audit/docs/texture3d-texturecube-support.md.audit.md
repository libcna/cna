# Audit: docs/texture3d-texturecube-support.md

## Metadata
- Source file: `docs/texture3d-texturecube-support.md` (142 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (source-inspected against Tasks 271-279, Phase 33)
- Cross-references: `docs/surface-format-support.md` (same "format param ignored, always RGBA8"
  pattern, audited in this same batch); `docs/rendertarget-support.md` (a sibling document from a
  slightly later phase covering `RenderTargetCube`, not plain `TextureCube` — disjoint scope,
  audited earlier in this pass)

## Purpose
Documents `Texture3D`/`TextureCube` construction/mip/format support per backend, and — the
document's most significant finding — that `GetData` (readback) is a **total silent no-op** on both
Vulkan and Bgfx, discovered by direct backend-source inspection rather than a failing test (since no
existing test asserts on `GetData`'s actual returned values).

## Executive Verdict
A genuinely important, high-severity-adjacent finding, documented with real rigor: the "Why the
existing test suite didn't already catch this" section is precisely the kind of self-critical gap
analysis this audit values — it doesn't just report the bug, it explains the exact test-design
reason (`GetData*` tests are argument-guard-only, never assert on returned pixel values) that let a
silent no-op pass "34/34 tests pass" for an unknown but plausibly long time.

## Checklist Results
- The `GetData` no-op finding is mechanistically precise: "falls through to `ITexture3DBackend`/
  `ITextureCubeBackend`'s base-class default (`virtual void GetData(...) const {}`, an empty body)"
  — not zeroed, just leaves the caller's buffer completely untouched, no exception, no log. This is
  a real, silent correctness hazard for any game code that calls `GetData` on Vulkan/Bgfx and trusts
  the result.
- The document's own 🔍 legend ("same code pattern as a confirmed bug elsewhere, but not
  independently reproduced with a test — flagged as likely, not confirmed") is applied with real
  discipline throughout the mip-level table — e.g. Vulkan/Bgfx `Texture3D`/`TextureCube` mip-level
  gaps are marked 🔍 (same code shape as the one confirmed-and-fixed EasyGL `TextureCube` bug,
  Task 276) rather than being asserted as confirmed bugs without a reproducing test.
- The `Texture3D` sampling gap (§"Sampling in shaders") correctly identifies a structural, not
  per-backend, root cause: `Texture3D`/`TextureCube` don't inherit `Texture` in CNA, so neither can
  be placed into `GraphicsDevice.Textures[slot]` — this is a precise, falsifiable architectural
  claim, and the document correctly notes `TextureCube` sampling *does* work for
  `EnvironmentMapEffect` specifically because that stock effect bypasses `GraphicsDevice.Textures`
  via a dedicated `GpuDrawParams::envMap` field — a genuinely useful distinction between "the general
  mechanism is broken" and "this one specific effect has its own bypass."
- The `DDSFromStreamEXT` finding (a confirmed non-functional stub returning a blank 1×1 cube map
  regardless of input) is a precise, falsifiable claim about a real gap between the API's apparent
  contract and its actual behavior.

## Detailed Findings
None against this document — its own findings (Vulkan/Bgfx `GetData` no-op, mip-level gaps, no
`Texture3D` shader sampling anywhere, `DDSFromStreamEXT` stub) are all real, already-tracked (Tasks
862-865), and not contradicted by this audit's own `xna-graphics` shard review.

## Cross-File Observations
Shares the "format parameter silently ignored, always RGBA8" pattern with `docs/surface-format-support.md`
(audited in this same batch) — both documents independently confirm the same underlying architectural
limitation from different angles (2D texture vs. 3D/cube texture), reinforcing that this is one
consistent, cross-texture-type gap rather than isolated incidents. Distinct in scope from
`docs/rendertarget-support.md`'s own `RenderTargetCube` findings (Tasks 873-881) — that document
covers render-*into*-a-cube-face; this one covers a plain, non-render-target `TextureCube`'s own
GetData/mip/sampling gaps — no overlap or double-counting between the two.

## Missing or Weak Tests
The document's own account is itself the finding here: `Texture3DTests.cpp`/`TextureCubeTests.cpp`'s
`GetData*` tests are argument-guard-only, never asserting on actual returned pixel values — a real,
now-documented test-coverage gap that let a silent no-op ship undetected.

## Positive Findings
The "Why the existing test suite didn't already catch this" self-analysis is exactly the kind of
retrospective test-design critique this audit's own methodology tries to apply — identifying not
just the bug but the systemic reason the existing test suite structurally couldn't have caught it.

## Final Assessment
No findings against this document. It documents a real, important, already-tracked gap (Vulkan/Bgfx
`Texture3D`/`TextureCube::GetData` total silent no-op) with unusually strong self-critical analysis
of why the existing test suite missed it.
