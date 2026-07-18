# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/sprite2d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/sprite2d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SpriteBatch vertex stage (2D pixel-space-to-NDC transform for sprite quads)
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SpriteEffect.fx (implicit; FNA's SpriteBatch uses its own simplified 2D transform, not fog-aware)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Maps pixel-space position directly to NDC, forwards UV and per-vertex Color. Explicitly documents a genuine, deliberate D3D-specific Y-flip (the opposite direction from every 3D shader's own Vulkan-only flip) since there is no projection matrix here to absorb the difference.

## Executive Verdict

**Healthy — correctly implemented, exceptionally well-documented backend-specific deviation.**

## Checklist Results

### API / XNA parity
Correct pixel-space-to-NDC mapping; UV/Color pass-through unchanged.

### Architecture
**Verified correct and necessary**: line 39 `output.Position = float4(ndc.x, -ndc.y, 0.0, 1.0);` negates NDC.y, the opposite of what the 3D shaders in this directory need (they *omit* Vulkan's flip; this one *adds* one D3D never needed from Vulkan's own convention, because sprite rendering bypasses the projection-matrix trick every 3D shader uses to absorb the Y-convention difference). The header comment's explanation of exactly why this shader needs the opposite treatment from every 3D sibling is accurate and one of the clearest pieces of backend-difference documentation in the whole directory.

### Systematic FNA parity gaps
No fog term at all — correct and consistent with FNA, which does not fog SpriteBatch sprites — matches FNA, whose SpriteBatch has no fog support.

## Detailed Findings

None.

## Cross-File Observations

Its own header comment cross-references the opposite convention used by every 3D vertex shader in this directory, correctly explaining why the two need different treatment rather than presenting them as inconsistent.

## Missing or Weak Tests

No SpriteBatch orientation/upside-down-rendering regression test specific to this shader was found, though general SpriteBatch rendering is covered extensively elsewhere in the codebase.

## Positive Findings

A model example of documenting a genuine, necessary backend-specific deviation rather than leaving it as an unexplained difference from the Vulkan source.

## Final Assessment

No defects found.
