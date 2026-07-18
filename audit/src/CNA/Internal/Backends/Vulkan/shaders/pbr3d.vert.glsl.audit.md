# Audit: src/CNA/Internal/Backends/Vulkan/shaders/pbr3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/pbr3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: PbrEffect vertex shader — stride 48 (VertexPositionNormalTangentTexture), glTF metallic-roughness BRDF vertex stage.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

PbrEffect vertex shader — stride 48 (VertexPositionNormalTangentTexture), glTF metallic-roughness BRDF vertex stage.

## Executive Verdict

Needs attention — NEW instance of the missing-Y-flip bug, confirmed via direct source read, with a plausible-but-incomplete justifying comment.

## Checklist Results

### Behavioral correctness / FNA parity
**Confirmed: lacks the Y-flip.** `gl_Position = pc.mvp * vec4(aPos, 1.0);` — no follow-up flip, despite receiving the identical `wvp`/`mvp` push-constant field (filled by the same `FillExtPushConst()` C++ function) that `lit_textured3d.vert.glsl`/`skinned3d.vert.glsl`/every other sibling DOES flip. The file's own comment ("kept consistent with pbr3d_skinned.vert.glsl's own convention... so PbrEffect and SkinnedPbrEffect render an identical scene identically oriented") only checks internal consistency between the 2 PBR shaders — it does not address consistency against every OTHER effect type sharing the same input, which do flip. Net effect: PbrEffect scenes render vertically mirrored relative to BasicEffect/SkinnedEffect/DualTextureEffect scenes in the same Vulkan-rendered frame. Normal-transform math itself is correct (inverse-transpose), and PBR fog formula/tangent handling are both correct. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup (now 4 confirmed Vulkan effect-shader families affected by this bug class).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

**Confirmed: lacks the Y-flip.** `gl_Position = pc.mvp * vec4(aPos, 1.0);` — no follow-up flip, despite receiving the identical `wvp`/`mvp` push-constant field (filled by the same `FillExtPushConst()` C++ function) that `lit_textured3d.vert.glsl`/`skinned3d.vert.glsl`/every other sibling DOES flip. The file's own comment ("kept consistent with pbr3d_skinned.vert.glsl's own convention... so PbrEffect and SkinnedPbrEffect render an identical scene identically oriented") only checks internal consistency between the 2 PBR shaders — it does not address consistency against every OTHER effect type sharing the same input, which do flip. Net effect: PbrEffect scenes render vertically mirrored relative to BasicEffect/SkinnedEffect/DualTextureEffect scenes in the same Vulkan-rendered frame. Normal-transform math itself is correct (inverse-transpose), and PBR fog formula/tangent handling are both correct. See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup (now 4 confirmed Vulkan effect-shader families affected by this bug class).

## Cross-File Observations

`pbr3d_skinned.vert.glsl` shares this exact omission and additionally contains a factually FALSE justifying comment — see that file's own report.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Normal-transform (inverse-transpose), tangent handling, and PBR fog formula are all correct.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
